# WinMTR 集成纯真社区版 CZDB IP 库

把 WinMTR 的「IP 归属」查询，从已停更的 `QQWry.dat`（纯真 2021 版）改为调用
**纯真社区版 CZDB** 二进制库（`cz88_public_v4.czdb` + 官方 C 解析 SDK）。

## 改动点

- `WinMTRNet.cpp` 的 `DnsResolverThread`：原来 `fopen("QQWry.dat")` 做二分查找，
  改为调用 CZDB 的 `initDBSearcher / search / closeDBSearcher`。
- 每个 hop 的归属串在构造时初始化一次 `DBSearcher`（**MEMORY 模式，整库载入内存**，线程安全），
  所有 `DnsResolverThread` 共享同一实例并发查询。
- CZDB 返回 **UTF-8**（如 `中国–江苏–南京\t南京信风网络科技有限公司GreatbitDNS服务器`）。
  代码先把 en dash(`–`,U+2013) 与 `\t` 规整成 ASCII `-`/` `，再转成本地代码页(GBK)，
  才能在 WinMTR 的 MultiByte 列表控件正常显示中文。
- 密钥取自同目录 `czdb.key`（纯文本）；文件不存在时用 `WinMTRNet.cpp` 里的
  `DEFAULT_CZDB_KEY` 兜底。

## 你需要准备的（构建侧）

1. 用 vcpkg + CMake 构建 `czdb-search-c`，得到 3 个文件：
   - `db_searcher.dll`
   - `db_searcher.lib`
   - （运行期依赖）`libcrypto-*.dll`、`libssl-*.dll`（OpenSSL）、`msgpack.dll`（如动态链接）

   步骤（Windows，需 Visual Studio 2017+ 与 CMake）：
   ```bat
   git clone https://github.com/tagphi/czdb-search-c.git
   cd czdb-search-c
   vcpkg install openssl:x64-windows msgpack-c:x64-windows
   mkdir build && cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_GENERATOR_PLATFORM=x64
   cmake --build . --config Release
   ```
   产物在 `build/Release/`（或 `build/`）下。

2. 把上面文件放进本目录下的 `czdb_lib/`：
   - `czdb_lib/db_searcher.lib`        （链接用）
   - `czdb_lib/db_searcher.dll`        （运行期，本工程 Post-Build 会自动拷到输出目录）
   - `czdb_lib/libcrypto-*.dll`、`czdb_lib/libssl-*.dll`、`czdb_lib/msgpack.dll`（运行期依赖，同样会被拷走）

3. 把数据文件放到 **WinMTR.exe 同目录**：
   - `cz88_public_v4.czdb`
   - `czdb.key`（已随源码提供；如需换密钥直接改这个文件，不必重编）

## 工程已改

- `WinMTR.vcxproj`：4 个配置均追加了 `db_searcher.lib` 依赖、`$(ProjectDir)czdb_lib`
  附加库目录，以及 Post-Build 事件（把 `czdb_lib/*.dll` 拷到输出目录）。
- 只要 `czdb_lib/` 下 DLL 与 LIB 齐全，直接用 VS 打开 `WinMTR.sln` 编译即可。

## 运行期说明

- `db_searcher.dll` 及其 OpenSSL/msgpack 依赖必须与 `WinMTR.exe` 同目录，否则程序无法启动。
- 若 `cz88_public_v4.czdb` 缺失或 `czdb.key` 密钥错误，`InitCzdb()` 会失败，
  Hostname 列退回只显示 IP（不影响 tracert/ping 功能）。
- 库文件更新：直接替换同目录的 `cz88_public_v4.czdb` 即可，无需改代码。
