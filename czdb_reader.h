#pragma once
#ifndef CZDB_READER_H
#define CZDB_READER_H

#ifdef __cplusplus
extern "C" {
#endif

// 纯真 CZDB（cz88_public_v4.czdb）离线归属查询 —— 零依赖移植版：
// 使用 Windows BCrypt 做 AES-128-ECB（仅解密文件头小块），索引/数据用 XOR；
// 归属记录用极简 msgpack 解析。无需 OpenSSL / msgpack-c。
//
// czdb_open: 以 base64 license key 打开数据库文件，成功返回不透明句柄，失败返回 NULL。
// czdb_search: 查询点分十进制 IPv4，输出归属字符串（UTF-8）到 outBuf。
//             成功返回 0，未命中或失败返回负数。
// czdb_close: 释放资源。
void* czdb_open(const char* dbPath, const char* keyB64);
int   czdb_search(void* handle, const char* ip, char* outBuf, int outLen);
void  czdb_close(void* handle);

#ifdef __cplusplus
}
#endif

#endif // CZDB_READER_H
