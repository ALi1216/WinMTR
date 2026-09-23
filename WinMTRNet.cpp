//*****************************************************************************
// FILE:            WinMTRNet.cpp
//
//*****************************************************************************
#include "WinMTRGlobal.h"
#include "WinMTRNet.h"
#include "WinMTRDialog.h"
#include <iostream>
#include <sstream>
#include <string>

#include "stdio.h"
#include "string.h"

#include "czdb_bridge.h"

// 纯真社区版 CZDB 库配置
#define CZDB_DB_FILENAME "cz88_public_v4.czdb"
// 默认密钥（同目录 czdb.key 优先；此处仅作兜底）
#define DEFAULT_CZDB_KEY "HziL4SVpdbboh4rfgjRwiA=="


#define TRACE_MSG(msg)										\
	{														\
	std::ostringstream dbg_msg(std::ostringstream::out);	\
	dbg_msg << msg << std::endl;							\
	OutputDebugString(dbg_msg.str().c_str());				\
	}

#define IPFLAG_DONT_FRAGMENT	0x02
#define MAX_HOPS				30

struct trace_thread {
	int			address;
	WinMTRNet	*winmtr;
	int			ttl;
};

struct dns_resolver_thread {
	int			index;
	WinMTRNet	*winmtr;
};

void TraceThread(void *p);
void DnsResolverThread(void *p);

WinMTRNet::WinMTRNet(WinMTRDialog *wp) {
	
	ghMutex = CreateMutex(NULL, FALSE, NULL);
	tracing=false;
	initialized = false;
	wmtrdlg = wp;
	m_czdb = NULL;   // 防御：构造提前返回时也要保证为 NULL，避免 DnsResolverThread/析构误用野指针
	WSADATA wsaData;

    if( WSAStartup(MAKEWORD(2, 2), &wsaData) ) {
        AfxMessageBox("Failed initializing windows sockets library!");
		return;
    }

    hICMP_DLL =  LoadLibrary(_T("ICMP.DLL"));
    if (hICMP_DLL == 0) {
        AfxMessageBox("Failed: Unable to locate ICMP.DLL!");
        return;
    }

    /* 
     * Get pointers to ICMP.DLL functions
     */
    lpfnIcmpCreateFile  = (LPFNICMPCREATEFILE)GetProcAddress(hICMP_DLL,"IcmpCreateFile");
    lpfnIcmpCloseHandle = (LPFNICMPCLOSEHANDLE)GetProcAddress(hICMP_DLL,"IcmpCloseHandle");
    lpfnIcmpSendEcho    = (LPFNICMPSENDECHO)GetProcAddress(hICMP_DLL,"IcmpSendEcho");
    if ((!lpfnIcmpCreateFile) || (!lpfnIcmpCloseHandle) || (!lpfnIcmpSendEcho)) {
        AfxMessageBox("Wrong ICMP.DLL system library !");
        return;
    }

    /*
     * IcmpCreateFile() - Open the ping service
     */
    hICMP = (HANDLE) lpfnIcmpCreateFile();
    if (hICMP == INVALID_HANDLE_VALUE) {
        AfxMessageBox("Error in ICMP.DLL !");
        return;
    }

	ResetHops();

	InitCzdb();

	initialized = true;
	return;
}

WinMTRNet::~WinMTRNet()
{
	if(initialized) {
		/*
		 * IcmpCloseHandle - Close the ICMP handle
		 */
		lpfnIcmpCloseHandle(hICMP);

		// Shut down...
		FreeLibrary(hICMP_DLL);

		WSACleanup();

		CloseCzdb();

		CloseHandle(ghMutex);
	}
}

void WinMTRNet::ResetHops()
{
	for(int i = 0; i < MaxHost;i++) {
		host[i].addr = 0;
		host[i].xmit = 0;
		host[i].returned = 0;
		host[i].total = 0;
		host[i].last = 0;
		host[i].best = 0;
		host[i].worst = 0;
		memset(host[i].name,0,sizeof(host[i].name));
	}
}

void WinMTRNet::DoTrace(int address)
{
	HANDLE hThreads[MAX_HOPS];
	tracing = true;

	ResetHops();

	last_remote_addr = address;

	// one thread per TTL value
	for(int i = 0; i < MAX_HOPS; i++) {
		trace_thread *current = new trace_thread;
		current->address = address;
		current->winmtr = this;
		current->ttl = i + 1;
		hThreads[i] = (HANDLE)_beginthread(TraceThread, 0 , current);
	}

	WaitForMultipleObjects(MAX_HOPS, hThreads, TRUE, INFINITE);
}

void WinMTRNet::StopTrace()
{
	tracing = false;
}

void TraceThread(void *p)
{
	trace_thread* current = (trace_thread*)p;
	WinMTRNet *wmtrnet = current->winmtr;
	TRACE_MSG("Threaad with TTL=" << current->ttl << " started.");

    IPINFO			stIPInfo, *lpstIPInfo;
    DWORD			dwReplyCount;
	char			achReqData[8192];
	int				nDataLen									= wmtrnet->wmtrdlg->pingsize;
	char			achRepData[sizeof(ICMPECHO) + 8192];


    /*
     * Init IPInfo structure
     */
    lpstIPInfo				= &stIPInfo;
    stIPInfo.Ttl			= current->ttl;
    stIPInfo.Tos			= 0;
    stIPInfo.Flags			= IPFLAG_DONT_FRAGMENT;
    stIPInfo.OptionsSize	= 0;
    stIPInfo.OptionsData	= NULL;

    for (int i=0; i<nDataLen; i++) achReqData[i] = 32; //whitespaces

    while(wmtrnet->tracing) {
	    
		// For some strange reason, ICMP API is not filling the TTL for icmp echo reply
		// Check if the current thread should be closed
		if( current->ttl > wmtrnet->GetMax() ) break;

		// NOTE: some servers does not respond back everytime, if TTL expires in transit; e.g. :
		// ping -n 20 -w 5000 -l 64 -i 7 www.chinapost.com.tw  -> less that half of the replies are coming back from 219.80.240.93
		// but if we are pinging ping -n 20 -w 5000 -l 64 219.80.240.93  we have 0% loss
		// A resolution would be:
		// - as soon as we get a hop, we start pinging directly that hop, with a greater TTL
		// - a drawback would be that, some servers are configured to reply for TTL transit expire, but not to ping requests, so,
		// for these servers we'll have 100% loss
		dwReplyCount = wmtrnet->lpfnIcmpSendEcho(wmtrnet->hICMP, current->address, achReqData, nDataLen, lpstIPInfo, achRepData, sizeof(achRepData), ECHO_REPLY_TIMEOUT);

		PICMPECHO icmp_echo_reply = (PICMPECHO)achRepData;

		wmtrnet->AddXmit(current->ttl - 1);
		if (dwReplyCount != 0) {
			TRACE_MSG("TTL " << current->ttl << " reply TTL " << icmp_echo_reply->Options.Ttl << " Status " << icmp_echo_reply->Status << " Reply count " << dwReplyCount);

			switch(icmp_echo_reply->Status) {
				case IP_SUCCESS:
				case IP_TTL_EXPIRED_TRANSIT:
					wmtrnet->SetLast(current->ttl - 1, icmp_echo_reply->RoundTripTime);
					wmtrnet->SetBest(current->ttl - 1, icmp_echo_reply->RoundTripTime);
					wmtrnet->SetWorst(current->ttl - 1, icmp_echo_reply->RoundTripTime);
					wmtrnet->AddReturned(current->ttl - 1);
					wmtrnet->SetAddr(current->ttl - 1, icmp_echo_reply->Address);
				break;
				case IP_BUF_TOO_SMALL:
					wmtrnet->SetName(current->ttl - 1, "Reply buffer too small.");
				break;
				case IP_DEST_NET_UNREACHABLE:
					wmtrnet->SetName(current->ttl - 1, "Destination network unreachable.");
				break;
				case IP_DEST_HOST_UNREACHABLE:
					wmtrnet->SetName(current->ttl - 1, "Destination host unreachable.");
				break;
				case IP_DEST_PROT_UNREACHABLE:
					wmtrnet->SetName(current->ttl - 1, "Destination protocol unreachable.");
				break;
				case IP_DEST_PORT_UNREACHABLE:
					wmtrnet->SetName(current->ttl - 1, "Destination port unreachable.");
				break;
				case IP_NO_RESOURCES:
					wmtrnet->SetName(current->ttl - 1, "Insufficient IP resources were available.");
				break;
				case IP_BAD_OPTION:
					wmtrnet->SetName(current->ttl - 1, "Bad IP option was specified.");
				break;
				case IP_HW_ERROR:
					wmtrnet->SetName(current->ttl - 1, "Hardware error occurred.");
				break;
				case IP_PACKET_TOO_BIG:
					wmtrnet->SetName(current->ttl - 1, "Packet was too big.");
				break;
				case IP_REQ_TIMED_OUT:
					wmtrnet->SetName(current->ttl - 1, "Request timed out.");
				break;
				case IP_BAD_REQ:
					wmtrnet->SetName(current->ttl - 1, "Bad request.");
				break;
				case IP_BAD_ROUTE:
					wmtrnet->SetName(current->ttl - 1, "Bad route.");
				break;
				case IP_TTL_EXPIRED_REASSEM:
					wmtrnet->SetName(current->ttl - 1, "The time to live expired during fragment reassembly.");
				break;
				case IP_PARAM_PROBLEM:
					wmtrnet->SetName(current->ttl - 1, "Parameter problem.");
				break;
				case IP_SOURCE_QUENCH:
					wmtrnet->SetName(current->ttl - 1, "Datagrams are arriving too fast to be processed and datagrams may have been discarded.");
				break;
				case IP_OPTION_TOO_BIG:
					wmtrnet->SetName(current->ttl - 1, "An IP option was too big.");
				break;
				case IP_BAD_DESTINATION:
					wmtrnet->SetName(current->ttl - 1, "Bad destination.");
				break;
				case IP_GENERAL_FAILURE:
					wmtrnet->SetName(current->ttl - 1, "General failure.");
				break;
				default:
					wmtrnet->SetName(current->ttl - 1, "General failure.");
			}

			if(wmtrnet->wmtrdlg->interval * 1000 > icmp_echo_reply->RoundTripTime)
				Sleep(wmtrnet->wmtrdlg->interval * 1000 - icmp_echo_reply->RoundTripTime);
		}

    } /* end ping loop */

	TRACE_MSG("Thread with TTL=" << current->ttl << " stopped.");

	delete p;
	_endthread();
}

int WinMTRNet::GetAddr(int at)
{
	WaitForSingleObject(ghMutex, INFINITE);
	int addr = ntohl(host[at].addr);
	ReleaseMutex(ghMutex);
	return addr;
}

int WinMTRNet::GetName(int at, char *n)
{
	WaitForSingleObject(ghMutex, INFINITE);
	if(!strcmp(host[at].name, "")) {
		int addr = GetAddr(at);
		sprintf (	n, "%d.%d.%d.%d", 
							(addr >> 24) & 0xff, 
							(addr >> 16) & 0xff, 
							(addr >> 8) & 0xff, 
							addr & 0xff
		);
		if(addr==0)
			strcpy(n,"");
	} else {
		strcpy(n, host[at].name);
	}
	ReleaseMutex(ghMutex);
	return 0;
}

int WinMTRNet::GetBest(int at)
{
	WaitForSingleObject(ghMutex, INFINITE);
	int ret = host[at].best;
	ReleaseMutex(ghMutex);
	return ret;
}

int WinMTRNet::GetWorst(int at)
{
	WaitForSingleObject(ghMutex, INFINITE);
	int ret = host[at].worst;
	ReleaseMutex(ghMutex);
	return ret;
}

int WinMTRNet::GetAvg(int at)
{
	WaitForSingleObject(ghMutex, INFINITE);
	int ret = host[at].returned == 0 ? 0 : host[at].total / host[at].returned;
	ReleaseMutex(ghMutex);
	return ret;
}

int WinMTRNet::GetPercent(int at)
{
	WaitForSingleObject(ghMutex, INFINITE);
	int ret = (host[at].xmit == 0) ? 0 : (100 - (100 * host[at].returned / host[at].xmit));
	ReleaseMutex(ghMutex);
	return ret;
}

int WinMTRNet::GetLast(int at)
{
	WaitForSingleObject(ghMutex, INFINITE);
	int ret = host[at].last;
	ReleaseMutex(ghMutex);
	return ret;
}

int WinMTRNet::GetReturned(int at)
{ 
	WaitForSingleObject(ghMutex, INFINITE);
	int ret = host[at].returned;
	ReleaseMutex(ghMutex);
	return ret;
}

int WinMTRNet::GetXmit(int at)
{ 
	WaitForSingleObject(ghMutex, INFINITE);
	int ret = host[at].xmit;
	ReleaseMutex(ghMutex);
	return ret;
}

int WinMTRNet::GetMax()
{
	WaitForSingleObject(ghMutex, INFINITE);
	int max = MAX_HOPS;

	// first match: traced address responds on ping requests, and the address is in the hosts list
	for(int i = 0; i < MAX_HOPS; i++) {
		if(host[i].addr == last_remote_addr) {
			max = i + 1;
			break;
		}
	}

	// second match:  traced address doesn't responds on ping requests
	if(max == MAX_HOPS) {
		while((max > 1) && (host[max - 1].addr == host[max - 2].addr) && (host[max - 1].addr != 0) ) max--;
	}

	ReleaseMutex(ghMutex);
	return max;
}

void WinMTRNet::SetAddr(int at, __int32 addr)
{
	WaitForSingleObject(ghMutex, INFINITE);
	if(host[at].addr == 0 && addr != 0) {
		TRACE_MSG("Start DnsResolverThread for new address " << addr << ". Old addr value was " << host[at].addr);
		host[at].addr = addr;
		dns_resolver_thread *dnt = new dns_resolver_thread;
		dnt->index = at;
		dnt->winmtr = this;
		if(wmtrdlg->useDNS) _beginthread(DnsResolverThread, 0, dnt);
	}

	ReleaseMutex(ghMutex);
}

void WinMTRNet::SetName(int at, char *n)
{
	WaitForSingleObject(ghMutex, INFINITE);
	strcpy(host[at].name, n);
	ReleaseMutex(ghMutex);
}

void WinMTRNet::SetBest(int at, int current)
{
	WaitForSingleObject(ghMutex, INFINITE);
	if(host[at].best > current || host[at].xmit == 1) {
		host[at].best = current;
	};
	if(host[at].worst < current) {
		host[at].worst = current;
	}

	ReleaseMutex(ghMutex);
}

void WinMTRNet::SetWorst(int at, int current)
{
	WaitForSingleObject(ghMutex, INFINITE);
	ReleaseMutex(ghMutex);
}

void WinMTRNet::SetLast(int at, int last)
{
	WaitForSingleObject(ghMutex, INFINITE);
	host[at].last = last;
	host[at].total += last;
	ReleaseMutex(ghMutex);
}

void WinMTRNet::AddReturned(int at)
{
	WaitForSingleObject(ghMutex, INFINITE);
	host[at].returned++;
	ReleaseMutex(ghMutex);
}

void WinMTRNet::AddXmit(int at)
{
	WaitForSingleObject(ghMutex, INFINITE);
	host[at].xmit++;
	ReleaseMutex(ghMutex);
}


// ---------- 纯真社区版 CZDB 辅助函数 ----------

// 把 CZDB 返回的 UTF-8 归属串中的 en dash(U+2013, "–") 与 "\t" 规整成 ASCII '-' / ' '，
// 便于后续转本地代码页（中文 Windows 的 GBK 不含 en dash）。
static void CzdbNormalize(const char* utf8, char* out, int outLen)
{
    int j = 0;
    for (int i = 0; utf8[i] != '\0' && j < outLen - 1; ) {
        unsigned char c = (unsigned char)utf8[i];
        if (c == 0xE2 && (unsigned char)utf8[i+1] == 0x80 && (unsigned char)utf8[i+2] == 0x93) {
            out[j++] = '-'; i += 3;
        } else if (utf8[i] == '\t') {
            out[j++] = ' '; i += 1;
        } else {
            out[j++] = utf8[i++];
        }
    }
    out[j] = '\0';
}

// UTF-8 -> 本地 ANSI 代码页（中文 Windows 为 GBK），供 WinMTR 的 MultiByte 列表控件显示
static void CzdbUtf8ToLocal(const char* utf8, char* out, int outLen)
{
    if (outLen <= 1) { if (outLen == 1) out[0] = '\0'; return; }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen <= 0) { out[0] = '\0'; return; }
    wchar_t* w = new wchar_t[wlen];
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
    WideCharToMultiByte(CP_ACP, 0, w, -1, out, outLen, "?", NULL);
    delete[] w;
}

// 取得当前 exe 所在目录（带结尾反斜杠）
static void CzdbGetExeDir(char* out, int outLen)
{
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = '\0';
    strncpy(out, path, outLen - 1);
    out[outLen - 1] = '\0';
}

// 读取同目录下的 czdb.key（纯文本密钥）；不存在或为空则用编译期默认密钥
static void CzdbLoadKey(char* out, int outLen)
{
    char dir[MAX_PATH] = {0};
    CzdbGetExeDir(dir, MAX_PATH);
    char keyfile[MAX_PATH] = {0};
    strncat(keyfile, dir, MAX_PATH - 1);
    strncat(keyfile, "czdb.key", MAX_PATH - 1 - strlen(keyfile));

    FILE* fp = fopen(keyfile, "rb");
    if (fp) {
        int n = (int)fread(out, 1, outLen - 1, fp);
        fclose(fp);
        while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' || out[n-1] == ' ')) out[--n] = '\0';
        if (n > 0) return;
    }
    strncpy(out, DEFAULT_CZDB_KEY, outLen - 1);
    out[outLen - 1] = '\0';
}

void WinMTRNet::InitCzdb()
{
    m_czdb = NULL;
    char dir[MAX_PATH] = {0};
    CzdbGetExeDir(dir, MAX_PATH);
    char dbpath[MAX_PATH] = {0};
    strncat(dbpath, dir, MAX_PATH - 1);
    strncat(dbpath, CZDB_DB_FILENAME, MAX_PATH - 1 - strlen(dbpath));

    char key[256] = {0};
    CzdbLoadKey(key, sizeof(key));

    FILE* test = fopen(dbpath, "rb");
    if (!test) {
        TRACE_MSG("CZDB file not found: " << dbpath);
        return;
    }
    fclose(test);

    // 必须用 MEMORY 模式：线程安全（WinMTR 每跳启动一个 DnsResolverThread）
    m_czdb = initDBSearcher(dbpath, key, CZDB_MEMORY);
    if (!m_czdb) {
        TRACE_MSG("initDBSearcher failed (wrong key or corrupt db)");
    }
}

void WinMTRNet::CloseCzdb()
{
    if (m_czdb) {
        closeDBSearcher(m_czdb);
        m_czdb = NULL;
    }
}

void DnsResolverThread(void *p)
{
    TRACE_MSG("DNS resolver thread started.");
    dns_resolver_thread *dnt = (dns_resolver_thread*)p;
    WinMTRNet* wn = dnt->winmtr;

    char buf[100];
    int addr = wn->GetAddr(dnt->index);
    sprintf(buf, "%d.%d.%d.%d", (addr >> 24) & 0xff, (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff);

    if (wn->m_czdb != NULL) {
        char regionUtf8[512] = {0};
        if (search(buf, wn->m_czdb, regionUtf8, (int)sizeof(regionUtf8)) == 0 && regionUtf8[0] != '\0') {
            char norm[512] = {0};
            CzdbNormalize(regionUtf8, norm, (int)sizeof(norm));
            char local[512] = {0};
            CzdbUtf8ToLocal(norm, local, (int)sizeof(local));
            std::string h = std::string(buf) + " " + std::string(local);
            char *writable = new char[h.size() + 1];
            std::copy(h.begin(), h.end(), writable);
            writable[h.size()] = '\0';
            wn->SetName(dnt->index, writable);
            delete[] writable;
        } else {
            // search 异常：退回只显示 IP
            wn->SetName(dnt->index, buf);
        }
    } else {
        // CZDB 未初始化（DLL/库文件缺失）：退回只显示 IP
        wn->SetName(dnt->index, buf);
    }

    delete p;
    TRACE_MSG("DNS resolver thread stopped.");
    _endthread();
}
