# WinMTR（在线归属版 / ipshudi）

基于开源 [WinMTR](https://github.com/oott123/WinMTR) 的增强版，主要改进了每一跳的**地址归属识别**方式。

## 相比上版本的更新（v0.95）

- **归属识别改为实时线上查询**：不再依赖本地 IP 库文件（纯真/CZDB），改为运行时通过 `https://www.ipshudi.com/<ip>.htm` 实时查询，自动组合 **归属地 + 运营商 + IP类型** 显示在 Hostname 列。无需再手动更新 IP 库，免去 CZDB/纯真文件与密钥的维护。
- **新增 Options「Packets/test」选项**：可设置单次测试发送的包个数（`0` = 持续运行直到手动停止，等效 `mtr -c N`）。修复了旧版“发几个包就停止”的提前终止问题。
- **移除本地 IP 库构建依赖**：不再需要 vcpkg / OpenSSL / msgpack / db_searcher.dll，只需系统自带的 `wininet.lib`，编译链大幅简化。
- 版本号提升为 **v0.95**。

## 相比上版本的更新（v0.96）

- **仅公网 IP 做在线归属查询**：新增内网/保留地址识别（RFC1918、loopback、link-local、CGNAT、组播/保留等），这类跳点不再发起联网查询，直接显示「局域网IP（Private-Use）」；只有公网 IP 才走 ipshudi 实时查询。
- 版本号提升为 **v0.96**。

## 相比上版本的更新（v0.97）

- **修复内网标注乱码**：中文标签改用 `\u` 转义书写，不再受编译机代码页影响，「局域网IP（Private-Use）」显示正常。
- **归属信息前显示所查 IP**：格式为「IP 归属地 运营商 IP类型」，便于与列表行对应。
- **修复自动停止后控件状态不复位**：达到「Packets/test」单次测试包数自动停止后，Stop 按钮恢复为 Start、Options 按钮恢复可点击（新增 `TRACING_TO_IDLE` 状态迁移）。
- **标题栏版本号改为动态取自 `WINMTR_VERSION`**，修复标题栏仍显示 v0.93 的旧硬编码。
- 版本号提升为 **v0.97**。

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
