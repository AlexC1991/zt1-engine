# Engine Build Resources

This folder contains all the build scripts for the Zoo Tycoon 1 Open Source Engine.

## Quick Start

Run `BUILD_ENGINE.bat` in the project root folder to run all steps automatically.

## Individual Steps

You can run each step individually if needed:

| Script | Description |
|--------|-------------|
| `step1_clean.py` | Cleans the build workspace (preserves fonts) |
| `step2_patch.py` | Applies source code patches for bug fixes |
| `step3_build.py` | Compiles the engine using CMake |
| `step4_import.py` | Imports assets from original Zoo Tycoon |
| `step5_setup.py` | Sets up runtime (fonts, config, folders) |
| `build_all.py` | Runs all steps in sequence |

## Requirements

- Python 3.x
- CMake
- Visual Studio (with C++ build tools)
- Original Zoo Tycoon game files (for step 4)

## Fonts

Place these font files in the `fonts/` folder in the project root:
- `Aileron-Black.otf`
- `Aileron-Bold.otf`
- `Aileron-Regular.otf`

Download from: https://www.fontsquirrel.com/fonts/aileron
