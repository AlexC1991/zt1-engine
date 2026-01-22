# UI System & Window Architecture

This document archives the layouts and logic required to recreate the ZT1 user interface.

## 1. Window Coordinate System
The original engine uses a screen-relative coordinate system (usually based on 800x600 or 1024x768). For the 2025 remake, these must be implemented as anchored UI elements to support modern widescreen resolutions.

## 2. Graphic Resource Types
| Asset Type | Extension | Usage in Remake |
| :--- | :--- | :--- |
| Buttons | .BMP / .TGA | Spritesheet with 4 states: Normal, Hover, Pressed, Disabled |
| Windows | .TGA | Nine-slice scaling textures for panels |
| Cursors | .CUR | Native SDL_Cursor implementation |

## 3. The 'Creator' UI Logic: Button Mapping
The engine links buttons to code via unique IDs. Many of these correspond to the String IDs we documented in `lang.dll`:
- **IDs 1000-1100:** Main bottom-bar tool buttons (Terrain, Foliage, Animals).
- **IDs 2000-2100:** View controls (Zoom, Rotate, Grid Toggle).
- **IDs 5000+:** Scenario-specific messages and objectives.
