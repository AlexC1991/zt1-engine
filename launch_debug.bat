@echo off
title OpenZT Runtime Debugger
color 0E
cd /d "%~dp0"

echo ==========================================
echo        LAUNCHING GAME IN DEBUG MODE
echo ==========================================
echo.
echo [INFO] Redirecting output to runtime_log.txt...
echo.

:: Launch the game and capture EVERYTHING (stdout and stderr)
if exist "build\Release\zt1-engine.exe" (
    cd build\Release
    
    :: Run game and pipe output to log, but ALSO show it on screen
    zt1-engine.exe > ..\..\runtime_log.txt 2>&1
    
    echo.
    echo [STATUS] Game Process Exited.
    echo.
) else (
    color 0C
    echo [ERROR] Could not find zt1-engine.exe in build\Release!
)

echo [INFO] Dumping Log Output:
echo -----------------------------------------------------------
type ..\..\runtime_log.txt
echo -----------------------------------------------------------
echo.
pause