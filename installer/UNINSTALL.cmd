@echo off
rem Session Studio and Session Anim Loader: uninstaller. Double-click this file.
rem It runs uninstall.ps1, a plain text script: open it in Notepad first if you want to see what it does.
rem Uninstalling may delete the folder this file is in, so everything is first copied to a temporary folder, and this
rem file hands over to that copy (named uninstall-run.cmd).
if /i "%~n0"=="uninstall-run" goto run
set "SSU_SRC=%~dp0"
if exist "%~dp0files\uninstall.ps1" set "SSU_SRC=%~dp0files\"
if not exist "%SSU_SRC%uninstall.ps1" goto missing
if not exist "%SSU_SRC%common.ps1" goto missing
set "SSU_RUN=%TEMP%\SessionStudio-uninstall-%RANDOM%%RANDOM%"
mkdir "%SSU_RUN%" || goto missing
copy /y "%SSU_SRC%uninstall.ps1" "%SSU_RUN%\" >nul || goto missing
copy /y "%SSU_SRC%common.ps1" "%SSU_RUN%\" >nul || goto missing
copy /y "%~f0" "%SSU_RUN%\uninstall-run.cmd" >nul || goto missing
cd /d "%TEMP%"
rem no "call": this file is finished with, the temporary copy takes over (its first argument is where we came from)
"%SSU_RUN%\uninstall-run.cmd" "%~dp0." %*

:run
powershell -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0uninstall.ps1" -From %*
exit /b %errorlevel%

:missing
echo.
echo   The uninstaller's files are missing or could not be copied.
echo   Extract the WHOLE package zip again and run UNINSTALL.cmd from there.
echo.
pause
exit /b 1
