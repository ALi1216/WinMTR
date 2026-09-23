@echo off
REM ============================================================================
REM  WinMTR + 纯真社区版 CZDB 一键构建脚本（Windows）
REM  前置依赖（需自行安装）：
REM    1. Visual Studio 2017+（勾选“使用 C++ 的桌面开发”+ MFC + Windows 10/11 SDK）
REM    2. CMake         https://cmake.org/
REM    3. vcpkg         https://github.com/microsoft/vcpkg  （装好后设置 VCPKG_ROOT）
REM    4. Git
REM  用法：
REM    set VCPKG_ROOT=C:\vcpkg
REM    build_winmtr.bat
REM  说明：本脚本会编译纯真官方 CZDB 解析库 db_searcher.dll，并将其与本工程链接，
REM        最终产物在 Release_x64\WinMTR.exe。
REM  注意：脚本未在作者沙箱中实跑（无 MSVC 工具链），首次运行请按提示修正路径。
REM ============================================================================
setlocal
set ARCH=x64
if "%VCPKG_ROOT%"=="" set VCPKG_ROOT=C:\vcpkg
set CZDB_SRC=%TEMP%\czdb-search-c
set OUT=czdb_lib

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
  echo [!] 未找到 vcpkg，请设置 VCPKG_ROOT 环境变量
  exit /b 1
)

echo [1/5] vcpkg 安装 OpenSSL / msgpack-c ...
call "%VCPKG_ROOT%\vcpkg.exe" install openssl:%ARCH%-windows msgpack-c:%ARCH%-windows

echo [2/5] 获取 CZDB C SDK 源码 ...
if not exist "%CZDB_SRC%" git clone --depth 1 https://github.com/tagphi/czdb-search-c.git "%CZDB_SRC%"

echo [3/5] 构建 db_searcher.dll + db_searcher.lib ...
rmdir /s /q "%CZDB_SRC%\build" 2>nul
mkdir "%CZDB_SRC%\build"
pushd "%CZDB_SRC%\build"
cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DCMAKE_GENERATOR_PLATFORM=%ARCH%
cmake --build . --config Release
popd

echo [4/5] 拷贝 db_searcher 与运行期依赖到 %OUT% ...
if not exist %OUT% mkdir %OUT%
copy /Y "%CZDB_SRC%\build\Release\db_searcher.dll" %OUT%\ 2>nul
copy /Y "%CZDB_SRC%\build\Release\db_searcher.lib" %OUT%\ 2>nul
copy /Y "%CZDB_SRC%\build\db_searcher.dll"        %OUT%\ 2>nul
copy /Y "%CZDB_SRC%\build\db_searcher.lib"        %OUT%\ 2>nul
for %%D in (libcrypto libssl libeay32 ssleay32 msgpack) do (
  copy /Y "%VCPKG_ROOT%\installed\%ARCH%-windows\bin\%%D*.dll" %OUT%\ 2>nul
)

echo [5/5] 构建 WinMTR.exe ...
set MSBUILD=
for /f "tokens=*" %%i in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\MSBuild.exe 2^>nul') do set MSBUILD=%%i
if "%MSBUILD%"=="" (
  for /r "%ProgramFiles(x86)%\Microsoft Visual Studio" %%i in (MSBuild.exe) do set MSBUILD=%%i
)
if "%MSBUILD%"=="" (
  echo [!] 未找到 msbuild，请在“VS 开发人员命令提示符”下运行本脚本
  exit /b 1
)
"%MSBUILD%" WinMTR.sln /p:Configuration=Release /p:Platform=%ARCH% /m

echo.
echo 完成。产物：Release_%ARCH%\WinMTR.exe
echo 运行前请把 cz88_public_v4.czdb 与 czdb.key 放到 WinMTR.exe 同目录。
endlocal
