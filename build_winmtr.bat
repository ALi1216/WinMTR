@echo off
REM ============================================================================
REM  WinMTR 一键构建脚本（Windows）—— 在线归属版（无本地 IP 库依赖）
REM  前置依赖（需自行安装）：
REM    1. Visual Studio 2017+（勾选“使用 C++ 的桌面开发” + MFC + Windows 10/11 SDK）
REM  说明：
REM    - 归属识别改为实时线上查询（ipshudi.com），不再需要本地 CZDB / 纯真库文件。
REM    - 仅需系统自带的 wininet.lib，无需 vcpkg / OpenSSL / msgpack。
REM  用法：
REM    build_winmtr.bat
REM  最终产物：Release_x64\WinMTR.exe
REM ============================================================================
setlocal
set ARCH=x64

echo [1/2] 定位 MSBuild ...
set MSBUILD=
for /f "tokens=*" %%i in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\MSBuild.exe 2^>nul') do set MSBUILD=%%i
if "%MSBUILD%"=="" (
  for /r "%ProgramFiles(x86)%\Microsoft Visual Studio" %%i in (MSBuild.exe) do set MSBUILD=%%i
)
if "%MSBUILD%"=="" (
  echo [!] 未找到 msbuild，请在“VS 开发人员命令提示符”下运行本脚本
  exit /b 1
)

echo [2/2] 构建 WinMTR.exe（Release %ARCH%，静态 MFC）...
"%MSBUILD%" WinMTR.sln /p:Configuration=Release /p:Platform=%ARCH% /m
if errorlevel 1 (
  echo [!] 构建失败
  exit /b 1
)

echo.
echo 完成。产物：Release_%ARCH%\WinMTR.exe
echo 归属识别为联网实时查询，运行时需可访问 https://www.ipshudi.com/ 。
endlocal
