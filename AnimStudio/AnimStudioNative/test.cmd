@echo off
rem Builds and runs asn_tests.exe (tests outside the game). Same build folder as build.cmd.
setlocal
call "D:\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
set B=%LOCALAPPDATA%\AnimStudio\build
cmake -S . -B "%B%" -G "Visual Studio 17 2022" -A x64 >nul || exit /b 3
cmake --build "%B%" --config Release --target asn_tests -- /m /v:minimal /nologo || exit /b 4
"%B%\Release\asn_tests.exe" %*
