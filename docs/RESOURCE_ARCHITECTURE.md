# Resource Architecture & Asset Mapping

This document archives the blueprint for asset management in the 2025 remake.

## 1. Asset Archive Hierarchy
The engine prioritizes assets based on the following loading sequence documented in the DLL resource strings:
1. **Update Archives (`*.zup`):** High priority patches.
2. **Expansion Archives (`*.ztd`):** Content from Dino Digs/Marine Mania.
3. **Base Archives (`terrain.ztd`, `ui.ztd`):** Core engine assets.

## 2. String Resource Mapping (Internal ID to UI)
The engine uses `LoadStringW` to fetch UI labels. This logic must be remade using a decoupled JSON or XML map for 2025 stability:
- **3000-3999:** Terrain names and descriptions.
- **6000-6999:** Scenery and building names.
- **9000-9999:** General terrain tool labels.

## 3. The 'Creator' Logic: Animation Data Structure
How a world object is actually 'created' in memory:
- **Object Pointer:** 4-byte address in the Entity Manager.
- **Sprite Reference:** Points to an entry in `SpriteDatabase` (mapped in Phase 2).
- **Animation State:** A state machine matching `AnimState` enum to specific `.ani` file paths.

## 4. Documentation of Graphics Handlers
| Handler | Format | 2025 Implementation Path |
| :--- | :--- | :--- |
| Terrain | .TGA / .PAL | SDL_Texture with Palette Swapping |
| Sprites | .ANI / .SPR | Sequence-based frame buffer |
| UI | .BMP (DirectDraw) | Modern RGBA Texture Sampling |
