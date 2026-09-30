# WinMTR（在线归属版 / ipshudi）

基于开源 [WinMTR](https://github.com/oott123/WinMTR) 的增强版，主要改进了每一跳的**地址归属识别**方式。

## 相比上版本的更新（v0.95）

- **归属识别改为实时线上查询**：不再依赖本地 IP 库文件（纯真/CZDB），改为运行时通过 `https://www.ipshudi.com/<ip>.htm` 实时查询，自动组合 **归属地 + 运营商 + IP类型** 显示在 Hostname 列。无需再手动更新 IP 库，免去 CZDB/纯真文件与密钥的维护。
- **新增 Options「Packets/test」选项**：可设置单次测试发送的包个数（`0` = 持续运行直到手动停止，等效 `mtr -c N`）。修复了旧版“发几个包就停止”的提前终止问题。
- **移除本地 IP 库构建依赖**：不再需要 vcpkg / OpenSSL / msgpack / db_searcher.dll，只需系统自带的 `wininet.lib`，编译链大幅简化。
- 版本号提升为 **v0.95**。

## 用法

1. 从 [Releases](https://github.com/ALi1216/WinMTR/releases) 下载 `WinMTR.exe`；
2. 直接运行，输入目标域名/IP 开始测试；
3. 归属识别为联网实时查询，运行时需可访问 `https://www.ipshudi.com/`；若查询失败，该跳点退化为仅显示 IP。

## 编译

- **GitHub Actions（推荐）**：推送到 `master` 自动在 `windows-latest` 用 VS2022 + 静态 MFC 编译，产物见 Artifacts `WinMTR-x64`。
- **本地**：双击运行 `build_winmtr.bat`（需已安装“使用 C++ 的桌面开发”+ MFC 的 Visual Studio 2017+），产物位于 `Release_x64\WinMTR.exe`。

## 说明

- 本项目已有编译好的版本，参见 [Releases](https://github.com/ALi1216/WinMTR/releases) 页面。
- 原始实现与讨论可参考 [V2EX 相关帖子](https://www.v2ex.com/t/176537#reply10)。
- 原始许可：GPL V2（Appnor MSP）。
