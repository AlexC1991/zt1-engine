@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Zoo Tycoon 1 Engine - Start

rem ===================================================================
rem  START.BAT - one button for a fresh clone.
rem
rem    1. finds (or asks for) your original Zoo Tycoon folder
rem    2. imports the game files
rem    3. builds the engine
rem    4. sets up the runtime  (needs the exe from step 3)
rem    5. launches it
rem
rem  The folder question is remembered in build\zt1_game_dir.txt, so it
rem  only ever asks once.
rem
rem  Options:
rem    start.bat --setup     only import the game files, then exit
rem    start.bat --rebuild   force a full clean rebuild
rem    start.bat --clean     delete the build folder and exit
rem ===================================================================

set "PY="
where py >NUL 2>&1 && set "PY=py -3"
if not defined PY (
    where python >NUL 2>&1 && set "PY=python"
)
if not defined PY (
    echo.
    echo  [X] Python was not found.
    echo      Install Python 3 from https://www.python.org/downloads/
    echo      and tick "Add python.exe to PATH", then run start.bat again.
    echo.
    pause
    exit /b 1
)

set "ARGS=%*"
if /i "%ARGS%"=="--clean" goto CLEAN

rem --- 1+2. find the game files and import them ------------------------
echo.
%PY% "engine-build-resources\start.py" %ARGS%
if errorlevel 1 (
    echo.
    echo  [X] Setup failed. See the messages above.
    echo.
    pause
    exit /b 1
)

if /i "%ARGS%"=="--setup" (
    echo.
    echo  Setup finished. Run start.bat again to build and launch.
    echo.
    pause
    exit /b 0
)

rem --- 3. build ---------------------------------------------------------
echo.
echo  ==========================================
echo      Building the engine
echo  ==========================================
rem The prebuilt release has the exe but no source: nothing to build
if not exist "CMakeLists.txt" (
    if exist "build\Release\zt1-engine.exe" (
        echo  [OK] Prebuilt engine found - no build needed.
        goto SETUP
    )
    echo.
    echo  [X] No source code and no prebuilt zt1-engine.exe in build\Release.
    echo.
    pause
    exit /b 1
)
if /i "%ARGS%"=="--rebuild" (
    if exist "build\Release" (
        rem keep the imported game files; drop everything compiled
        for /d %%D in ("build\*") do if /i not "%%~nxD"=="Release" rmdir /s /q "%%D"
        del /q "build\*.*" 2>NUL
    )
)

rem step3_build.py also recompiles any source whose CONTENT changed (files
rem unpacked from a zip keep old timestamps, which plain cmake --build misses)
%PY% "engine-build-resources\step3_build.py"
if errorlevel 1 goto BUILDFAIL

if not exist "build\Release\zt1-engine.exe" (
    echo.
    echo  [X] The build reported success but zt1-engine.exe is missing.
    echo.
    pause
    exit /b 1
)

:SETUP
rem --- 4. runtime setup (step 5 needs the exe) --------------------------
set "ZT1_NO_LAUNCH_PROMPT=1"
%PY% "engine-build-resources\step5_setup.py"
if errorlevel 1 goto SETUPWARN

rem --- 5. launch --------------------------------------------------------
echo.
echo  [OK] Launching Zoo Tycoon...
echo.
cd /d "build\Release"
zt1-engine.exe
cd /d "%~dp0"
exit /b 0

:SETUPWARN
echo.
echo  [!] Runtime setup reported a problem. See the messages above.
echo      The engine may still run - trying anyway.
echo.
if exist "build\Release\zt1-engine.exe" (
    cd /d "build\Release"
    zt1-engine.exe
    cd /d "%~dp0"
)
exit /b 0

:BUILDFAIL
echo.
echo  [X] Build failed. Fix the errors above and run start.bat again.
echo.
pause
exit /b 1

:CLEAN
echo.
rem The prebuilt release has no source to rebuild the exe from: keep it
if not exist "CMakeLists.txt" if exist "build\Release\zt1-engine.exe" (
    echo  Removing the imported game files and settings ^(keeping the engine^)...
    copy /y "build\Release\zt1-engine.exe" "%TEMP%\zt1-engine.keep.exe" >NUL
    rmdir /s /q "build"
    mkdir "build\Release"
    move /y "%TEMP%\zt1-engine.keep.exe" "build\Release\zt1-engine.exe" >NUL
    if not exist "build\Release\zt1-engine.exe" (
        echo  [X] Could not put zt1-engine.exe back - it is in %TEMP%\zt1-engine.keep.exe
        exit /b 1
    )
    echo  [OK] Done. Run start.bat to import your game files again.
    echo.
    exit /b 0
)
echo  Removing the build folder...
if exist "build" rmdir /s /q "build"
echo  [OK] Done. Run start.bat to build from scratch.
echo.
exit /b 0
