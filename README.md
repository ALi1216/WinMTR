# WinMTR（纯真 CZDB 本地归属 + 在线兜底）

基于开源 [WinMTR](https://github.com/oott123/WinMTR) 的增强版，主要改进了每一跳的**地址归属识别**方式：本地纯真数据库优先（即时、离线可用），库未命中时再走一遍在线实时查询兜底。

## 最新版 v0.94（相对 v0.93 的更新）

- **归属识别：纯真 CZDB 本地优先 + 在线兜底**：公网跳点优先查询本地纯真数据库 `cz88_public_v4.czdb`（内存模式整体读入，即时返回「归属地 运营商」，离线即用、格式统一）；库未命中的地址再通过 `https://www.ipshudi.com/<ip>.htm` 实时查询一次兜底，彻底消除“裸 IP 定格”与“残缺归属”问题。
- **零依赖内置 CZDB 阅读器（相对 v0.93 的主要变化）**：v0.93 借助外部 `db_searcher.dll`（vcpkg 构建的 CZDB SDK）做离线归属；v0.94 改为内置移植 `czdb_reader.cpp`，复用官方 `czdb-search-c` 的 v4 算法——文件头小块用 Windows `BCrypt` 做 AES-128-ECB 解密、索引/数据采用 XOR、归属记录用极简内置 msgpack 解析器；**不再需要 `db_searcher.dll`**，也不引入 OpenSSL / msgpack-c，静态 MFC(`/MT`) 编译链保持干净。
- **密钥外置（不发布）**：CZDB v4 解密密钥不再硬编码进源码，改为运行时读取 `WinMTR.exe` 同目录的 `czdb.key`（该文件已 gitignore，不入库、不随 Release 发布）；缺 `czdb.key` 时跳过离线回退，仅走在线查询。
- **加载路径仅 exe 同目录**：`cz88_public_v4.czdb` 与 `czdb.key` 均从 `WinMTR.exe` 所在目录读取（无桌面等硬编码回退路径）；数据库以内存模式整体读入，仅做只读查询，**多线程安全**。
- **内网/保留地址不联网**：识别 RFC1918、loopback、link-local、CGNAT、组播/保留等地址，直接显示「局域网IP（Private-Use）」，不发起在线查询；局域网跳点也显示所查 IP，与公网格式对齐。
- **其他稳定性修复**：每个公网跳点的归属查询均单次完成（本地库即时命中，在线仅库未命中时发起一遍、不重试）；修复 `czdb.key` base64 解码 bug（此前离线归属从未生效）与开库竞态（开库窗口内的查询不再永久丢失归属）；Host 输入框自动去前后空格；自动停止后控件状态正确复位；标题栏版本动态取自 `WINMTR_VERSION`。
- 版本号 **v0.94**（版本资源 0.94.0.0）。

## v0.93

- **归属识别改用纯真数据库（CZDB）**：通过外部 `db_searcher.dll`（CZDB SDK）实现离线归属查询，跳点显示「归属地 运营商」。
- 发布物含 `WinMTR.exe` + `db_searcher.dll`。

## 用法

1. 从 [Releases](https://github.com/ALi1216/WinMTR/releases) 下载最新版 **v0.94** 的 `WinMTR.exe`；
2. 直接运行，输入目标域名/IP 开始测试；
3. 归属识别优先使用本地纯真库（离线即用，无需联网）；库未命中的地址再走一遍联网实时查询兜底（需可访问 `https://www.ipshudi.com/`）。

> ⚠️ **纯真数据文件与密钥不随 Release 发布**（授权限制）：请将 `cz88_public_v4.czdb`（纯真 v4 格式离线库）与 `czdb.key`（对应许可密钥）自行获取后，放在 `WinMTR.exe` **同一目录**即可启用离线回退。仅放置 `WinMTR.exe` 也能正常运行，只是缺少离线兜底。

## 编译

- **GitHub Actions（推荐）**：推送到 `master` 自动在 `windows-latest` 用 VS2022 + 静态 MFC 编译，产物见 Artifacts `WinMTR-x64`。
- **本地**：双击运行 `build_winmtr.bat`（需已安装“使用 C++ 的桌面开发”+ MFC 的 Visual Studio 2017+），产物位于 `Release_x64\WinMTR.exe`。

## 说明

- 本项目已有编译好的版本，参见 [Releases](https://github.com/ALi1216/WinMTR/releases) 页面。
- 原始实现与讨论可参考 [V2EX 相关帖子](https://www.v2ex.com/t/176537#reply10)。
- 原始许可：GPL V2（Appnor MSP）。
