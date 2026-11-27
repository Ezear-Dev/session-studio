@echo off
rem Session Studio and Session Anim Loader: installer. Double-click this file.
rem It runs files\install.ps1, a plain text script: open it in Notepad first if you want to see what it does.
rem "-ExecutionPolicy Bypass" applies to this one run only; it changes no setting on your computer.
setlocal
if not exist "%~dp0files\install.ps1" (
    echo.
    echo   files\install.ps1 is missing.
    echo   Extract the WHOLE zip into a normal folder first ^(right-click the zip, "Extract All"^),
    echo   then run INSTALLER.cmd from that folder.
    echo.
    pause
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0files\install.ps1" %*
set RC=%errorlevel%
echo.
pause
exit /b %RC%
