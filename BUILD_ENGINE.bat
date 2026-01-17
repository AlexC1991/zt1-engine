@echo off
cd /d "%~dp0"

:MENU
echo ==========================================
echo      Zoo Tycoon 1 Engine Build Tool
echo ==========================================
echo [Q] Quick Build (Compile Changes & Run)
echo [F] Full Build  (Clean Rebuild via Python)
echo [E] Exit
echo ==========================================
set /p "Choice=Enter choice (Q, F, or E): "

if /i "%Choice%"=="Q" goto QUICK
if /i "%Choice%"=="F" goto FULL
if /i "%Choice%"=="E" goto END

echo Invalid choice. Please try again.
echo.
goto MENU

:FULL
python "engine-build-resources\build_all.py"
pause
goto END

:QUICK
cmake --build build --config Release --parallel
if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build failed. Game will not start.
    pause
    goto END
)
echo.
echo [SUCCESS] Starting Game...
cd /d build\Release
zt1-engine.exe
goto END

:END
exit /b