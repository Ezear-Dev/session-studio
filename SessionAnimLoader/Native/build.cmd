@echo off
rem Builds SessionAnimLoaderNative. The build folder goes to %LOCALAPPDATA%\SessionStudio\build (outside the project).
setlocal
call "D:\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist third_party\ue4ss\UE4SS.lib lib /nologo /def:third_party\ue4ss\UE4SS.def /out:third_party\ue4ss\UE4SS.lib /machine:x64 || exit /b 2
set B=%LOCALAPPDATA%\SessionStudio\build
cmake -S . -B "%B%" -G "Visual Studio 17 2022" -A x64 >nul || exit /b 3
cmake --build "%B%" --config Release -- /m /v:minimal /nologo || exit /b 4
if not exist dist mkdir dist
copy /y "%B%\Release\main.dll" dist\main.dll >nul || exit /b 5
dir dist\main.dll | findstr main.dll
rem the editor (built when SessionStudio\third_party is present)
if exist "%B%\editor\Release\SessionStudio.exe" (
  if not exist ..\..\SessionStudio\dist mkdir ..\..\SessionStudio\dist
  copy /y "%B%\editor\Release\SessionStudio.exe" ..\..\SessionStudio\dist\SessionStudio.exe >nul || exit /b 6
  dir ..\..\SessionStudio\dist\SessionStudio.exe | findstr SessionStudio.exe
)
