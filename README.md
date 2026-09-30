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

## 相比上版本的更新（v0.98）

- **局域网跳点也显示所查 IP**：内网/保留地址跳点的 Hostname 列由仅「局域网IP（Private-Use）」改为「**IP 局域网IP（Private-Use）**」，与公网「IP 归属地 运营商 IP类型」格式对齐，便于区分同一路径中的不同内网跳点。
- **Host 输入框自动去前后空格**：原 `OnRestart` 仅 `TrimLeft()` 且重复调用（bug），现改为 `Trim()` 去除前后空格，并把裁剪后的值写回输入框，点击 Start 即看到净化结果，避免误输入空格导致解析失败。
- 版本号提升为 **v0.98**。

## 相比上版本的更新（v0.99）

- **修复后探测出的跳点无归属**：归属查询只在每个跳点首次响应时执行一次，开局多个跳点并发查询时受 WinINet 每主机 2 并发连接上限与站点限流影响，部分查询超时失败后**永不重试**，这些跳点就永远只显示裸 IP（如 `182.61.250.196`）。现改为：
  - **串行化在线查询**：`LookupAttribution` 加进程级互斥锁，同一时刻只发一个请求，规避并发挤占；
  - **失败退避重试**：单个跳点归属查询最多重试 5 次（间隔 2/4/6/8s 递增），保证中途才探测出的地址也能补上归属；停止追踪后立即终止重试，不留残留线程。
- 版本号提升为 **v0.99**。

## 相比上版本的更新（v1.00）

- **新增离线归属回退（纯真 CZDB）**：在线查询（ipshudi）因站点限流/无法访问而拿不到归属时，自动回退到本地纯真数据库 `cz88_public_v4.czdb` 补全归属，彻底消除“裸 IP 定格”问题。
  - **零依赖移植**：复用官方 `czdb-search-c` 的 v4 算法——文件头小块用 Windows `BCrypt` 做 AES-128-ECB 解密、索引/数据采用 XOR、归属记录用极简内置 msgpack 解析器；不引入 OpenSSL / msgpack-c，静态 MFC(`/MT`) 编译链保持干净。
  - **加载路径**：优先读取 `WinMTR.exe` 同目录下的 `cz88_public_v4.czdb`，找不到则回退到桌面 `C:\Users\12788\Desktop\cz88_public_v4.czdb`；数据库以内存模式整体读入，仅做只读查询，**多线程安全**。
  - **仅作回退**：公网跳点仍优先走在线查询（更快、含运营商/IP类型）；仅在在线查询（含 5 次退避重试）彻底失败时才命中纯真库，输出格式与在线保持一致（「IP 归属地 运营商」）。
- 版本号提升为 **v1.00**。

## 用法

1. 从 [Releases](https://github.com/ALi1216/WinMTR/releases) 下载 `WinMTR.exe`；
2. 直接运行，输入目标域名/IP 开始测试；
3. 归属识别优先走联网实时查询（运行时需可访问 `https://www.ipshudi.com/`）；在线查询失败时自动回退到本地纯真库 `cz88_public_v4.czdb`（放在 `WinMTR.exe` 同目录或桌面）补全归属，无需联网也能显示归属。

## 编译

- **GitHub Actions（推荐）**：推送到 `master` 自动在 `windows-latest` 用 VS2022 + 静态 MFC 编译，产物见 Artifacts `WinMTR-x64`。
- **本地**：双击运行 `build_winmtr.bat`（需已安装“使用 C++ 的桌面开发”+ MFC 的 Visual Studio 2017+），产物位于 `Release_x64\WinMTR.exe`。

## 说明

- 本项目已有编译好的版本，参见 [Releases](https://github.com/ALi1216/WinMTR/releases) 页面。
- 原始实现与讨论可参考 [V2EX 相关帖子](https://www.v2ex.com/t/176537#reply10)。
- 原始许可：GPL V2（Appnor MSP）。
