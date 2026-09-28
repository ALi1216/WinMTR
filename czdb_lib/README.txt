把纯真社区版 CZDB 的 C SDK 编译产物放到本目录，WinMTR 构建时会自动链接并拷贝运行期 DLL。

需要放进来文件（x64 Release 为例）：
  db_searcher.lib        链接用（必须）
  db_searcher.dll        运行期（必须，Post-Build 会拷到 exe 输出目录）
  libcrypto-*.dll        OpenSSL 运行期依赖（必须，随 DLL 一起拷）
  libssl-*.dll           OpenSSL 运行期依赖（必须）
  msgpack.dll            msgpack-c 运行期依赖（若动态链接则需要）

构建 db_searcher 的方法见上层 czdb_integration.md。
