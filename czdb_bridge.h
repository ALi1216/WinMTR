//*****************************************************************************
// FILE:            czdb_bridge.h
//
// 纯真社区版 CZDB 解析库 (db_searcher.dll) 的极简 C 接口声明。
// 只声明 WinMTR 实际用到的 3 个导出函数，避免把 OpenSSL / msgpack 的头文件
// 拖进本工程参与编译（链接阶段只需 db_searcher.lib）。
//
// 库的完整源码：https://github.com/tagphi/czdb-search-c
// 文档：https://github.com/tagphi/czdb-search-c/blob/main/BUILD_WINDOWS.md
//*****************************************************************************
#ifndef CZDB_BRIDGE_H_
#define CZDB_BRIDGE_H_

#ifdef __cplusplus
extern "C" {
#endif

// 必须与 czdb-search-c 中 SearchType 的取值一致：MEMORY=0, BTREE=1
typedef enum { CZDB_MEMORY = 0, CZDB_BTREE = 1 } CzdbSearchType;

// 不透明指针，真实定义在 DLL 内部
typedef struct DBSearcher DBSearcher;

// 初始化解析器。返回 NULL 表示失败（库文件不存在 / 密钥错误 / DLL 缺失）。
// searchType 必须用 CZDB_MEMORY：BTREE 模式非线程安全，而 WinMTR 每个 hop 启一个线程。
DBSearcher* initDBSearcher(char* dbFilePath, char* key, CzdbSearchType searchType);

// 查询单个 IPv4。成功返回 0，region 填入以 '\0' 结尾的 UTF-8 字符串
// （格式：国家–省–市\t运营商，前三级用 en dash "–" 分隔，末级前为制表符）。
// 非 0 表示未命中或出错。
int search(char* ipString, DBSearcher* dbSearcher, char* region, int regionLen);

// 释放资源。
void closeDBSearcher(DBSearcher* dbSearcher);

#ifdef __cplusplus
}
#endif

#endif // ifndef CZDB_BRIDGE_H_
