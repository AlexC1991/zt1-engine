@echo off
:: Quote the title so the '&' symbol doesn't break the script
title "Zoo Tycoon Engine Builder & Asset Manager"
color 0A

:: CRITICAL FIX: Force the script to run in the folder where this file is saved
cd /d "%~dp0"

echo ========================================================
echo          OPEN ZOO TYCOON - AUTOMATED BUILDER
echo ========================================================
echo.

:: 1. CHECK FOR CMAKE
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    color 0C
    echo [ERROR] CMake is not installed or not in your PATH.
    echo Please install it from: https://cmake.org/download/
    echo.
    pause
    exit /b
)

:: 2. CREATE BUILD FOLDER
echo [STEP 1/4] Checking Build Directory...
if not exist build (
    mkdir build
    echo  - Created 'build' folder.
) else (
    echo  - 'build' folder already exists.
)

:: 3. GENERATE PROJECT FILES
echo.
echo [STEP 2/4] Generating Project Files...
cd build
cmake ..
if %errorlevel% neq 0 (
    cd ..
    color 0C
    echo.
    echo [ERROR] CMake generation failed.
    echo Ensure CMakeLists.txt is in the folder: %~dp0
    pause
    exit /b
)

:: 4. COMPILE THE GAME
echo.
echo [STEP 3/4] Compiling Engine (Release Mode)...
cmake --build . --config Release
if %errorlevel% neq 0 (
    cd ..
    color 0C
    echo.
    echo [ERROR] Compilation failed.
    pause
    exit /b
)

:: Go back to main folder to run python script
cd ..

:: 5. CHECK FOR ASSETS & AUTO-IMPORT
echo.
echo [STEP 4/4] Checking Game Assets...

:: Check if animals.ztd exists in the release folder
if not exist "build\Release\animals.ztd" (
    color 0E
    echo.
    echo [!] Assets missing (animals.ztd not found).
    echo [!] Launching Auto-Importer...
    echo.
    
    :: Check if Python is installed
    where python >nul 2>nul
    if %errorlevel% neq 0 (
        echo [ERROR] Python is not installed. Cannot run import_assets.py.
        echo Please manually copy your Zoo Tycoon files to: build\Release
    ) else (
        python import_assets.py
    )
) else (
    echo  - Assets found! Skipping import.
)

:: 6. FINISH
color 0A
echo.
echo ========================================================
echo               BUILD & SETUP COMPLETE!
echo ========================================================
echo.
echo You can play the game now:
echo    build\Release\zt1-engine.exe
echo.
pause