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
#include <wininet.h>

#include "stdio.h"
#include "string.h"
#include "czdb_reader.h"




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
	// 按"单次测试包个数"自动停止：>0 时每个跳点发送够即停（等同 mtr -c N）；=0 持续运行直到手动停止
	if (wmtrdlg->packetsPerTest > 0 && host[at].xmit >= wmtrdlg->packetsPerTest)
		tracing = false;
	ReleaseMutex(ghMutex);
}


// ---------- 实时在线归属识别（ipshudi.com）----------

// 从 HTML 中提取 <td class="th">LABEL</td> 行后 <td> 内 <span> 的文本
static bool ExtractField(const char* html, const char* label, char* out, int outLen)
{
    out[0] = 0;
    char pat[64];
    sprintf(pat, "<td class=\"th\">%s</td>", label);
    const char* p = strstr(html, pat);
    if (!p) return false;
    const char* td = strstr(p + strlen(pat), "<td");
    if (!td) return false;
    const char* vs = strchr(td, '>');
    if (!vs) return false;
    vs++;
    const char* end = strstr(vs, "</td>");
    if (!end) return false;
    // 优先取 <span>...</span>
    const char* span = strstr(vs, "<span>");
    const char* start = vs;
    if (span && span < end) {
        start = span + 6;
        const char* se = strstr(start, "</span>");
        if (se && se < end) end = se;
    }
    int n = 0; bool intag = false;
    for (const char* q = start; q < end && n < outLen - 1; q++) {
        if (*q == '<') intag = true;
        else if (*q == '>') intag = false;
        else if (!intag) out[n++] = *q;
    }
    out[n] = 0;
    while (n > 0 && (out[n-1]==' '||out[n-1]=='\n'||out[n-1]=='\r'||out[n-1]=='\t')) out[--n]=0;
    return n > 0;
}

// 判断是否为内网/保留地址（非公网），用于跳过无意义的在线归属查询
static bool IsPrivateOrReservedIP(unsigned long addr)
{
    unsigned char o1 = (unsigned char)((addr >> 24) & 0xff);
    unsigned char o2 = (unsigned char)((addr >> 16) & 0xff);
    unsigned char o3 = (unsigned char)((addr >> 8)  & 0xff);
    unsigned char o4 = (unsigned char)( addr        & 0xff);
    if (o1 == 0)   return true;                                   // 0.0.0.0/8
    if (o1 == 127) return true;                                   // loopback 127.0.0.0/8
    if (o1 == 10)  return true;                                   // RFC1918 10.0.0.0/8
    if (o1 == 172 && o2 >= 16 && o2 <= 31) return true;          // RFC1918 172.16.0.0/12
    if (o1 == 192 && o2 == 168) return true;                     // RFC1918 192.168.0.0/16
    if (o1 == 169 && o2 == 254) return true;                     // link-local 169.254.0.0/16
    if (o1 == 100 && o2 >= 64 && o2 <= 127) return true;         // CGNAT 100.64.0.0/10
    if (o1 == 255 && o2 == 255 && o3 == 255 && o4 == 255) return true; // 受限广播
    if (o1 >= 224) return true;                                   // 组播 224/4 与保留 240/4
    return false;
}

// 在线归属查询串行锁：WinINet 默认每主机仅 2 个并发连接，开局多个跳点同时查询时
// 后排请求会排队直至超时失败，这里串行化以保证每次查询的成功率
static HANDLE g_attrSerialMutex = NULL;

// 通过 https://www.ipshudi.com/<ip>.htm 实时查询归属地/运营商/IP类型，组合后写入 outAttr（本地 ANSI）
bool WinMTRNet::LookupAttribution(const char* ip, char* outAttr, int outLen)
{
    outAttr[0] = 0;
    if (!ip || !*ip) return false;

    char url[256];
    sprintf(url, "https://www.ipshudi.com/%s.htm", ip);

    // 命名互斥体：多线程同时首次创建时系统保证指向同一内核对象
    if (!g_attrSerialMutex)
        g_attrSerialMutex = CreateMutexA(NULL, FALSE, "Local\\WinMTR_AttrQuery");
    bool locked = (g_attrSerialMutex != NULL) && WaitForSingleObject(g_attrSerialMutex, 15000) == WAIT_OBJECT_0;

    char* html = NULL; bool ok = false;
    HINTERNET hOpen = InternetOpenA("WinMTR/0.95", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (hOpen) {
        DWORD timeout = 5000;
        InternetSetOptionA(hOpen, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(hOpen, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        HINTERNET hUrl = InternetOpenUrlA(hOpen, url, NULL, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_UI, 0);
        if (hUrl) {
            const DWORD cap = 65536;
            html = new char[cap + 1];
            if (html) {
                DWORD total = 0; char chunk[8192]; DWORD read;
                while (total < cap && InternetReadFile(hUrl, chunk, sizeof(chunk), &read) && read > 0) {
                    memcpy(html + total, chunk, read); total += read;
                }
                html[total] = 0;
                ok = (total > 0);
            }
            InternetCloseHandle(hUrl);
        }
        InternetCloseHandle(hOpen);
    }
    if (!ok || !html) { delete[] html; if (locked) ReleaseMutex(g_attrSerialMutex); return false; }

    char region[128] = {0}, isp[128] = {0}, iptype[128] = {0};
    ExtractField(html, "归属地", region, sizeof(region));
    ExtractField(html, "运营商", isp, sizeof(isp));
    ExtractField(html, "iP类型", iptype, sizeof(iptype));
    delete[] html;

    std::string combined;
    if (region[0]) combined += region;
    if (isp[0])    { if (!combined.empty()) combined += " "; combined += isp; }
    if (iptype[0]) { if (!combined.empty()) combined += " "; combined += iptype; }
    if (combined.empty()) { if (locked) ReleaseMutex(g_attrSerialMutex); return false; }

    // UTF-8 -> 本地 ANSI（中文 Windows 下为 GBK），供列表控件显示
    int wlen = MultiByteToWideChar(CP_UTF8, 0, combined.c_str(), -1, NULL, 0);
    if (wlen > 0) {
        wchar_t* w = new wchar_t[wlen];
        MultiByteToWideChar(CP_UTF8, 0, combined.c_str(), -1, w, wlen);
        WideCharToMultiByte(CP_ACP, 0, w, -1, outAttr, outLen, "?", NULL);
        delete[] w;
        bool ret = outAttr[0] != 0;
        if (locked) ReleaseMutex(g_attrSerialMutex);
        return ret;
    }
    if (locked) ReleaseMutex(g_attrSerialMutex);
    return false;
}

// ---------- 离线归属回退（纯真 CZDB）----------
// 当在线查询（ipshudi）因限流/无法访问而拿不到归属时，回退到本地纯真数据库
// cz88_public_v4.czdb。数据库以「内存模式」整体读入，仅做只读查询，多线程安全。
static void* g_czdbHandle = NULL;     // 已打开的句柄（一次性，之后只读）
static bool  g_czdbTried  = false;    // 是否已尝试打开（避免每次查询失败都重读文件）
static HANDLE g_czdbInitMutex = NULL;
static const char* CZDB_KEY = "HziL4SVpdbboh4rfgjRwiA==";

// 懒加载：exe 同目录优先，回退到桌面；仅打开一次
static bool EnsureCzdbOpened()
{
    if (g_czdbHandle != NULL) return true;
    if (g_czdbTried) return false;
    if (!g_czdbInitMutex)
        g_czdbInitMutex = CreateMutexA(NULL, FALSE, "Local\\WinMTR_CzdbInit");
    if (!g_czdbInitMutex) return false;
    if (WaitForSingleObject(g_czdbInitMutex, 5000) != WAIT_OBJECT_0) return false;

    bool ret = false;
    if (g_czdbHandle == NULL && !g_czdbTried) {
        g_czdbTried = true;   // 标记已尝试：无论成败都不再重试，防止线程残留/反复读盘
        char path[MAX_PATH] = {0};
        DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
        if (n > 0) {
            char* slash = strrchr(path, '\\');
            if (slash) strcpy(slash + 1, "cz88_public_v4.czdb");
        }
        void* h = NULL;
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
            h = czdb_open(path, CZDB_KEY);
        if (!h) {
            // 回退：桌面（用户本机常驻位置）
            strcpy(path, "C:\\Users\\12788\\Desktop\\cz88_public_v4.czdb");
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
                h = czdb_open(path, CZDB_KEY);
        }
        g_czdbHandle = h;
        ret = (h != NULL);
    } else {
        ret = (g_czdbHandle != NULL);
    }
    ReleaseMutex(g_czdbInitMutex);
    return ret;
}

// 离线归属查询：UTF-8 -> 本地 ANSI(GBK) 供列表控件显示
static bool CzdbLookup(const char* ip, char* outAttr, int outLen)
{
    outAttr[0] = 0;
    if (!EnsureCzdbOpened() || !g_czdbHandle) return false;
    char utf8[512] = {0};
    if (czdb_search(g_czdbHandle, ip, utf8, (int)sizeof(utf8)) != 0) return false;
    if (utf8[0] == 0) return false;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen > 0) {
        wchar_t* w = new wchar_t[wlen];
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
        WideCharToMultiByte(CP_ACP, 0, w, -1, outAttr, outLen, "?", NULL);
        delete[] w;
        return outAttr[0] != 0;
    }
    return false;
}

void DnsResolverThread(void *p)
{
    TRACE_MSG("DNS resolver thread started.");
    dns_resolver_thread *dnt = (dns_resolver_thread*)p;
    WinMTRNet* wn = dnt->winmtr;

    char buf[100];
    int addr = wn->GetAddr(dnt->index);
    sprintf(buf, "%d.%d.%d.%d", (addr >> 24) & 0xff, (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff);

    // 仅对公网 IP 发起在线归属查询；内网/保留地址直接标注，避免无效联网
    if (IsPrivateOrReservedIP((unsigned long)addr)) {
        // 用 \u 转义书写中文，源码编码差异（CI 为 en-US 代码页）不再导致乱码
        // L"局域网IP（Private-Use）"
        wchar_t labelW[] = L"\u5C40\u57DF\u7F51IP\uFF08Private-Use\uFF09";
        char labelAnsi[64] = {0};
        WideCharToMultiByte(CP_ACP, 0, labelW, -1, labelAnsi, (int)sizeof(labelAnsi), "?", NULL);
        // 局域网跳点同样展示所查 IP，格式对齐公网「IP 归属地 运营商 IP类型」
        char combined[255];
        snprintf(combined, sizeof(combined), "%s %s", buf, labelAnsi);
        wn->SetName(dnt->index, combined);
    } else {
        char attr[200] = {0};
        bool got = false;
        // 开局多个跳点并发查询易被 WinINet 并发上限/站点限流挤掉，单次失败会导致该跳点
        // 永远只显示裸 IP。这里带退避重试（最多 5 次，2/4/6/8s），保证后探测出的地址也能补上归属
        for (int attempt = 0; attempt < 5 && !got; attempt++) {
            if (attempt > 0) {
                if (!wn->tracing) break;   // 已停止追踪则不再重试，避免线程残留
                Sleep(2000 * attempt);
            }
            if (WinMTRNet::LookupAttribution(buf, attr, (int)sizeof(attr)) && attr[0] != '\0') got = true;
        }
        if (got) {
            // 归属信息前加上所查 IP，便于对应行
            char combined[255];
            snprintf(combined, sizeof(combined), "%s %s", buf, attr);
            wn->SetName(dnt->index, combined);
        } else {
            // 在线查询（含退避重试）仍失败：回退纯真 CZDB 离线库补全归属
            char czattr[512] = {0};
            if (CzdbLookup(buf, czattr, (int)sizeof(czattr)) && czattr[0] != '\0') {
                char combined[255];
                snprintf(combined, sizeof(combined), "%s %s", buf, czattr);
                wn->SetName(dnt->index, combined);
            } else {
                // 离线库也未命中：退回只显示 IP
                wn->SetName(dnt->index, buf);
            }
        }
    }

    delete p;
    TRACE_MSG("DNS resolver thread stopped.");
    _endthread();
}
