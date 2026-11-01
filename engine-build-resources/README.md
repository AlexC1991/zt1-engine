# Engine build resources

The player setup that `start.bat` (in the project root) runs on a fresh clone:

| Script | What it does |
|--------|--------------|
| `start.py` | Finds (or asks for) the Zoo Tycoon folder, then imports its files (step 4) |
| `step3_build.py` | Builds the engine with CMake into `build/Release/` |
| `step4_import.py` | Imports the game's files from the player's own install |
| `step5_setup.py` | Sets up the runtime: fonts, config, folders, button art |
| `zt1_paths.py` | Where the game and the build live |

Also here: `notices.py` and `fetch_licenses.py` (third-party notices for the
installers), `licenses/`, and `RELEASE_README.md` (the readme shipped in the
portable build).

The release tooling (making installers, signing update feeds, publishing) is
private and gitignored; it exists only on the release machine.

## Requirements

- Python 3, CMake, Visual Studio 2022 C++ build tools
- The player's own Zoo Tycoon install (for step 4)
- Fonts in `fonts/`: Aileron Black, Bold and Regular
  (https://www.fontsquirrel.com/fonts/aileron)
