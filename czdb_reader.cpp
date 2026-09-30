//*****************************************************************************
// FILE:            czdb_reader.cpp
//
// Zero-dependency reader for the 纯真 (CZDB) v4 offline IP geolocation database.
//
// It mirrors the on-disk algorithm of the official tagphi/czdb-search-c SDK,
// but replaces OpenSSL (AES-128-ECB, header block only) with the Windows
// BCrypt API and the msgpack-c dependency with a tiny built-in msgpack reader,
// so it links cleanly into the static-MFC /MT WinMTR build without pulling in
// extra third-party libraries. The result is fully thread-safe because we read
// the whole DB into memory once (MEMORY mode) and only ever read from it.
//*****************************************************************************
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "czdb_reader.h"


// ---- constants (mirror czdb_sdk) ----
#define CZDB_HEADER_SIZE          12
#define CZDB_SUPER_PART_LENGTH    17
#define CZDB_HEADER_BLOCK_LENGTH  20
#define CZDB_HEADER_BLOCK_PTR      9
#define CZDB_FILE_SIZE_PTR         1
#define CZDB_EXPIRATION_MASK    0xFFFFF
#define CZDB_IPV4                  4
#define CZDB_IPV6                  6


// ---- handle ----
struct CzdbHandle {
    unsigned char*  fileBuf;     // whole file (kept alive until close)
    unsigned char*  dbBin;       // == fileBuf + offset
    long            offset;
    int             ipType;
    int             ipBytesLength;
    int             indexLength;
    int             endIndexPtr;   // super-header @13: offset (from dbBin start) to end-index region
    int             headerLength;
    unsigned char** headerSip;   // pointers into fileBuf (NOT owned)
    int*            headerPtr;
    int             columnSelection;
    unsigned char*  geoMapData;  // XOR-decrypted geo map (owned)
    int             geoMapSize;
    unsigned char   key[16];
};


// ---- base64 (minimal, no OpenSSL) ----
static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
// Streaming bit-correct decode: '=' terminates payload, whitespace skipped.
// (Fixes two bugs of the previous version: lowercase letters were mapped with
//  c-'A'+52 instead of c-'a'+26, and padded tail groups emitted wrong bytes --
//  the AES key never decoded correctly, so CZDB offline lookup never worked.)
static int base64Decode(const char* in, int len, unsigned char* out, int outCap) {
    int o = 0; unsigned int v = 0; int bits = 0;
    for (int i = 0; i < len; i++) {
        char c = in[i];
        if (c == '=') break;                       // padding: end of payload
        int val = b64val(c);
        if (val < 0) continue;                     // skip CR/LF/space/unknown
        v = (v << 6) | (unsigned int)val; bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o < outCap) out[o++] = (unsigned char)((v >> bits) & 0xFF);
        }
    }
    return o;
}


// ---- AES-128-ECB via BCrypt (header block only) ----
static int aes128ecb_decrypt(const unsigned char* key16, const unsigned char* in, int inLen, unsigned char* out) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    NTSTATUS st;
    st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (st != 0) return -1;
    st = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB,
                           sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    if (st != 0) { BCryptCloseAlgorithmProvider(hAlg, 0); return -1; }
    st = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PUCHAR)key16, 16, 0);
    if (st != 0) { BCryptCloseAlgorithmProvider(hAlg, 0); return -1; }
    ULONG cb = 0;
    st = BCryptDecrypt(hKey, (PUCHAR)in, inLen, NULL, NULL, 0, (PUCHAR)out, inLen, &cb, 0);
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return (st == 0) ? (int)cb : -1;
}
// AES/ECB/PKCS5Padding was used at write time; strip PKCS7 after raw decrypt.
static int stripPkcs7(unsigned char* buf, int len) {
    if (len <= 0) return len;
    int p = buf[len - 1];
    if (p >= 1 && p <= 16 && p <= len) {
        bool all = true;
        for (int i = len - p; i < len; i++) if (buf[i] != (unsigned char)p) { all = false; break; }
        if (all) return len - p;
    }
    return len;
}


// ---- little-endian 4-byte int (mirror byte_utils.c) ----
static int getIntLong(const unsigned char* b, int off) {
    return (int)(
        ((b[off]     & 0x000000FFL))        |
        ((b[off + 1] << 8)  & 0x0000FF00L) |
        ((b[off + 2] << 16) & 0x00FF0000L) |
        ((b[off + 3] << 24) & 0xFF000000L)
    );
}
static int getInt1(const unsigned char* b, int off) {
    return (int)((unsigned char)b[off] & 0xFF);
}
// canonical signed-char comparison from czdb_sdk byte_utils.c
static int compareBytes(const unsigned char* b1, const unsigned char* b2, int n) {
    const signed char* a = (const signed char*)b1;
    const signed char* c = (const signed char*)b2;
    for (int i = 0; i < n; i++) {
        if (a[i] * c[i] > 0) {
            if (a[i] < c[i]) return -1;
            else if (a[i] > c[i]) return 1;
        } else if (a[i] * c[i] < 0) {
            if (a[i] > 0) return -1;
            else return 1;
        } else if (a[i] * c[i] == 0 && a[i] + c[i] != 0) {
            if (a[i] == 0) return -1;
            else return 1;
        }
    }
    return 0;
}
static void xorDecrypt(unsigned char* buf, int size, const unsigned char* key16) {
    for (int i = 0; i < size; i++) buf[i] ^= key16[i % 16];
}


// ---- tiny msgpack reader (positive ints / str / array only) ----
struct MpCursor { const unsigned char* p; const unsigned char* end; };

static int mpReadUint(MpCursor* c, unsigned long long* out) {
    if (c->p >= c->end) return -1;
    unsigned char b = *c->p++;
    if (b <= 0x7f) { *out = b; return 0; }
    if (b == 0xcc) { if (c->p + 1 > c->end) return -1; *out = c->p[0]; c->p += 1; return 0; }
    if (b == 0xcd) { if (c->p + 2 > c->end) return -1; *out = ((unsigned int)c->p[0] << 8) | c->p[1]; c->p += 2; return 0; }
    if (b == 0xce) { if (c->p + 4 > c->end) return -1; unsigned int v = 0; for (int i = 0; i < 4; i++) v = (v << 8) | c->p[i]; c->p += 4; *out = v; return 0; }
    if (b == 0xcf) { if (c->p + 8 > c->end) return -1; unsigned long long v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | c->p[i]; c->p += 8; *out = v; return 0; }
    return -1;
}
static int mpReadStr(MpCursor* c, const unsigned char** out, unsigned int* outLen) {
    if (c->p >= c->end) return -1;
    unsigned char b = *c->p++;
    unsigned int len;
    if (b >= 0xa0 && b <= 0xbf) len = b & 0x1f;
    else if (b == 0xc0) { *out = c->p; *outLen = 0; return 0; }   // nil -> empty
    else if (b == 0xd9) { if (c->p + 1 > c->end) return -1; len = c->p[0]; c->p += 1; }
    else if (b == 0xda) { if (c->p + 2 > c->end) return -1; len = ((unsigned int)c->p[0] << 8) | c->p[1]; c->p += 2; }
    else if (b == 0xdb) { if (c->p + 4 > c->end) return -1; len = ((unsigned int)c->p[0] << 24) | ((unsigned int)c->p[1] << 16) | ((unsigned int)c->p[2] << 8) | c->p[3]; c->p += 4; }
    else return -1;
    if (c->p + len > c->end) return -1;
    *out = c->p; *outLen = len; c->p += len;
    return 0;
}
static int mpReadArray(MpCursor* c, unsigned int* count) {
    if (c->p >= c->end) return -1;
    unsigned char b = *c->p++;
    if (b >= 0x90 && b <= 0x9f) { *count = b & 0x0f; return 0; }
    if (b == 0xdc) { if (c->p + 2 > c->end) return -1; *count = ((unsigned int)c->p[0] << 8) | c->p[1]; c->p += 2; return 0; }
    if (b == 0xdd) { if (c->p + 4 > c->end) return -1; *count = ((unsigned int)c->p[0] << 24) | ((unsigned int)c->p[1] << 16) | ((unsigned int)c->p[2] << 8) | c->p[3]; c->p += 4; return 0; }
    return -1;
}

static int getActualGeo(const unsigned char* geoMapData, int columnSelection, int geoPtr, int geoLen,
                        char* buf, int bufSize) {
    MpCursor c; c.p = geoMapData + geoPtr; c.end = geoMapData + geoPtr + geoLen;
    unsigned int count;
    if (mpReadArray(&c, &count) != 0) return -1;
    int remaining = bufSize - 1;
    int written = 0;
    for (unsigned int i = 0; i < count; i++) {
        const unsigned char* val; unsigned int vlen;
        if (mpReadStr(&c, &val, &vlen) != 0) return -1;
        bool selected = ((columnSelection >> (i + 1)) & 1) == 1;
        if (!selected) continue;
        const unsigned char* use = val; unsigned int useLen = vlen;
        if (vlen == 0) { use = (const unsigned char*)"null"; useLen = 4; }
        if ((int)useLen < remaining) {
            memcpy(buf + written, use, useLen);
            written += (int)useLen; remaining -= (int)useLen;
        } else { return -1; }
        if (1 < remaining) { buf[written++] = '\t'; remaining -= 1; }
        else { return -1; }
    }
    buf[written] = '\0';
    return written;
}

static int unpack(const unsigned char* geoMapData, int columnSelection,
                  const unsigned char* data, int dataLen, char* buf, int bufSize) {
    MpCursor c; c.p = data; c.end = data + dataLen;
    unsigned long long geoPosMix;
    if (mpReadUint(&c, &geoPosMix) != 0) return -1;
    int geoLen = (int)((geoPosMix >> 24) & 0xFF);
    int geoPtr = (int)(geoPosMix & 0x00FFFFFF);
    const unsigned char* otherData; unsigned int otherLen;
    if (mpReadStr(&c, &otherData, &otherLen) != 0) return -1;
    int written = 0;
    if (geoPosMix != 0 && geoMapData != NULL) {
        written = getActualGeo(geoMapData, columnSelection, geoPtr, geoLen, buf, bufSize);
        if (written == -1) return -1;
    }
    if (written + (int)otherLen < bufSize) {
        memcpy(buf + written, otherData, otherLen);
        written += (int)otherLen;
        buf[written] = '\0';
    } else { return -1; }
    return written;
}


// ---- IPv4 dot-quad parser (network byte order) ----
static int parseIPv4(const char* ip, unsigned char* out) {
    int parts[4]; int n = 0; int v = 0; bool have = false;
    for (const char* p = ip; ; p++) {
        if (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); have = true; }
        else if (*p == '.' || *p == '\0') {
            if (!have || n > 3) return -1;
            parts[n++] = v; v = 0; have = false;
            if (*p == '\0') break;
        } else return -1;
    }
    if (n != 4) return -1;
    for (int i = 0; i < 4; i++) { if (parts[i] < 0 || parts[i] > 255) return -1; out[i] = (unsigned char)parts[i]; }
    return 0;
}


// ---- binary tree search over the in-memory DB (MEMORY mode, thread-safe) ----
static int bTreeSearch(CzdbHandle* h, const char* ipString, char* region, int regionLen) {
    unsigned char ip[16];
    if (parseIPv4(ipString, ip) != 0) return -4;
    int n = h->ipBytesLength;
    memset(region, 0, regionLen);

    int l = 0, hi = h->headerLength - 1, sptr = 0, eptr = 0;
    while (l <= hi) {
        int m = (l + hi) / 2;
        int cmp = compareBytes(ip, h->headerSip[m], n);
        if (cmp < 0) hi = m - 1;
        else if (cmp > 0) l = m + 1;
        else { sptr = h->headerPtr[m > 0 ? m - 1 : m]; eptr = h->headerPtr[m]; break; }
    }
    if (l > hi) {
        if (l == 0 && hi <= 0) return -1;
        if (l < h->headerLength) { sptr = h->headerPtr[l - 1]; eptr = h->headerPtr[l]; }
        else if (hi >= 0 && hi + 1 < h->headerLength) { sptr = h->headerPtr[hi]; eptr = h->headerPtr[hi + 1]; }
        else { sptr = h->headerPtr[h->headerLength - 1]; eptr = sptr + h->indexLength; }
    }
    if (sptr == 0) return -1;

    int blockLen = eptr - sptr, blen = h->indexLength;
    const unsigned char* idx = h->dbBin + sptr;
    l = 0; hi = blockLen / blen;
    int dataPtr = 0, dataLen = 0;
    while (l <= hi) {
        int m = (l + hi) >> 1;
        int p = m * blen;
        int cmpS = compareBytes(ip, idx + p, n);
        int cmpE = compareBytes(ip, idx + p + n, n);
        if (cmpS >= 0 && cmpE <= 0) {
            dataPtr = getIntLong(idx, p + n * 2);
            dataLen = getInt1(idx, p + n * 2 + 4);
            break;
        } else if (cmpS < 0) hi = m - 1;
        else l = m + 1;
    }
    if (dataPtr == 0) return -1;
    if (dataLen > regionLen) return -2;

    const unsigned char* data = h->dbBin + dataPtr;
    return unpack(h->geoMapData, h->columnSelection, data, dataLen, region, regionLen);
}


//*****************************************************************************
// Public C API
//*****************************************************************************

void* czdb_open(const char* dbPath, const char* keyB64) {
    FILE* f = fopen(dbPath, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long fsize = ftell(f); fseek(f, 0, SEEK_SET);
    if (fsize <= 0) { fclose(f); return NULL; }
    unsigned char* fileBuf = (unsigned char*)malloc((size_t)fsize);
    if (!fileBuf) { fclose(f); return NULL; }
    if (fread(fileBuf, 1, (size_t)fsize, f) != (size_t)fsize) { free(fileBuf); fclose(f); return NULL; }
    fclose(f);

    CzdbHandle* h = (CzdbHandle*)calloc(1, sizeof(CzdbHandle));
    if (!h) { free(fileBuf); return NULL; }

    unsigned char keyDecoded[32];
    int keyLen = base64Decode(keyB64, (int)strlen(keyB64), keyDecoded, sizeof(keyDecoded));
    if (keyLen < 16) { free(fileBuf); free(h); return NULL; }
    memcpy(h->key, keyDecoded, 16);

    if (fsize < CZDB_HEADER_SIZE + 4) { free(fileBuf); free(h); return NULL; }
    int clientId = getIntLong(fileBuf, 4);
    int encBlockSize = getIntLong(fileBuf, 8);
    if (CZDB_HEADER_SIZE + encBlockSize > fsize) { free(fileBuf); free(h); return NULL; }

    unsigned char* enc = fileBuf + CZDB_HEADER_SIZE;
    unsigned char* dec = (unsigned char*)malloc(encBlockSize);
    if (!dec) { free(fileBuf); free(h); return NULL; }
    if (aes128ecb_decrypt(h->key, enc, encBlockSize, dec) < 0) { free(dec); free(fileBuf); free(h); return NULL; }
    int realDecLen = stripPkcs7(dec, encBlockSize);
    int decClientId = (getIntLong(dec, 0) >> 20) & 0xFFF;
    int randomSize  = getIntLong(dec, 4);
    free(dec);
    if (decClientId != clientId) { free(fileBuf); free(h); return NULL; }   // wrong key

    h->offset = CZDB_HEADER_SIZE + encBlockSize + randomSize;
    if (h->offset > fsize) { free(fileBuf); free(h); return NULL; }
    h->dbBin = fileBuf + h->offset;

    const unsigned char* sb = fileBuf + h->offset;
    if (h->offset + CZDB_SUPER_PART_LENGTH > fsize) { free(fileBuf); free(h); return NULL; }
    h->ipType = (sb[0] & 1) == 0 ? CZDB_IPV4 : CZDB_IPV6;
    h->ipBytesLength = h->ipType == CZDB_IPV4 ? 4 : 16;
    h->endIndexPtr = getIntLong(sb, 13);
    h->indexLength = h->ipBytesLength * 2 + 5;

    int totalHeaderBlockSize = getIntLong(sb, CZDB_HEADER_BLOCK_PTR);
    int fileSizeInFile = getIntLong(sb, CZDB_FILE_SIZE_PTR);
    long realFileSize = fsize - h->offset;
    if (fileSizeInFile != realFileSize) { free(fileBuf); free(h); return NULL; }

    const unsigned char* bblk = fileBuf + h->offset + CZDB_SUPER_PART_LENGTH;
    int nblocks = 0;
    for (int i = 0; i + CZDB_HEADER_BLOCK_LENGTH <= totalHeaderBlockSize; i += CZDB_HEADER_BLOCK_LENGTH) {
        if (getIntLong(bblk, i + 16) == 0) break;
        nblocks++;
    }
    h->headerLength = nblocks;
    h->headerSip = (unsigned char**)malloc(sizeof(unsigned char*) * (nblocks ? nblocks : 1));
    h->headerPtr = (int*)malloc(sizeof(int) * (nblocks ? nblocks : 1));
    if (!h->headerSip || !h->headerPtr) { czdb_close(h); return NULL; }
    int idx = 0;
    for (int i = 0; i + CZDB_HEADER_BLOCK_LENGTH <= totalHeaderBlockSize; i += CZDB_HEADER_BLOCK_LENGTH) {
        int dp = getIntLong(bblk, i + 16);
        if (dp == 0) break;
        h->headerSip[idx] = (unsigned char*)(bblk + i);
        h->headerPtr[idx] = dp;
        idx++;
    }

    // geo mapping (only present when columnSelection != 0; XOR-encrypted)
    int colSelPtr = h->offset + h->endIndexPtr + h->ipBytesLength * 2 + 5;
    if (colSelPtr + 4 <= fsize) {
        h->columnSelection = getIntLong(fileBuf, colSelPtr);
        if (h->columnSelection != 0) {
            int geoMapPtr = colSelPtr + 4;
            int geoMapSize = getIntLong(fileBuf, geoMapPtr);
            if (geoMapPtr + 4 + geoMapSize <= fsize && geoMapSize > 0) {
                h->geoMapData = (unsigned char*)malloc(geoMapSize);
                if (h->geoMapData) {
                    memcpy(h->geoMapData, fileBuf + geoMapPtr + 4, geoMapSize);
                    xorDecrypt(h->geoMapData, geoMapSize, h->key);
                }
            }
        }
    }

    h->fileBuf = fileBuf;   // keep alive
    return h;
}

int czdb_search(void* handle, const char* ip, char* outBuf, int outLen) {
    CzdbHandle* h = (CzdbHandle*)handle;
    if (!h || !ip || !outBuf || outLen <= 0) return -1;
    outBuf[0] = 0;
    char tmp[512];
    int r = bTreeSearch(h, ip, tmp, sizeof(tmp));
    if (r < 0) return r;
    // normalize for display: tab -> space, en/em dash (U+2013/U+2014) -> hyphen,
    // so the UTF-8 string converts cleanly to the local (GBK) code page.
    int o = 0;
    for (int i = 0; tmp[i] && o < outLen - 1; i++) {
        unsigned char c = (unsigned char)tmp[i];
        if (c == '\t') { outBuf[o++] = ' '; }
        else if (c == 0xE2 && (unsigned char)tmp[i + 1] == 0x80 &&
                 ((unsigned char)tmp[i + 2] == 0x93 || (unsigned char)tmp[i + 2] == 0x94)) {
            outBuf[o++] = '-'; i += 2;
        } else { outBuf[o++] = (char)c; }
    }
    outBuf[o] = 0;
    return 0;
}

void czdb_close(void* handle) {
    CzdbHandle* h = (CzdbHandle*)handle;
    if (!h) return;
    if (h->geoMapData) free(h->geoMapData);
    if (h->headerSip)  free(h->headerSip);
    if (h->headerPtr)  free(h->headerPtr);
    if (h->fileBuf)    free(h->fileBuf);
    free(h);
}
