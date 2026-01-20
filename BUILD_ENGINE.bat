@echo off
cd /d "%~dp0"

:: --- CLION / AUTOMATION SUPPORT ---
if /i "%1"=="Q" goto QUICK
if /i "%1"=="F" goto FULL
if /i "%1"=="V" goto VERIFY

:MENU
echo ==========================================
echo      Zoo Tycoon 1 Engine Build Tool
echo ==========================================
echo [Q] Quick Build  (Compile Changes ^& Run)
echo [V] Verify Build (Compile Changes ONLY)
echo [F] Full Build   (Clean Rebuild via Python)
echo [E] Exit
echo ==========================================
set /p "Choice=Enter choice (Q, V, F, or E): "

if /i "%Choice%"=="Q" goto QUICK
if /i "%Choice%"=="V" goto VERIFY
if /i "%Choice%"=="F" goto FULL
if /i "%Choice%"=="E" goto END

echo Invalid choice.
echo Please try again.
echo.
goto MENU

:FULL
python "engine-build-resources\build_all.py"
pause
goto END

:VERIFY
echo === VERIFYING BUILD (NO LAUNCH) ===
cmake --build build --config Release --parallel
if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build failed. Fix code errors above.
    pause
    goto MENU
)
echo.
echo [SUCCESS] Build Verified! No code errors found.
pause
goto MENU

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