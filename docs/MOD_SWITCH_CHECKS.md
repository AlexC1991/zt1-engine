# Mod switch checks — 27 October 2026

All gameplay checks use the ZT1 Lab MCP and the Lab's embedded engine. Builds use Write Code → Build. The normal player's save is opened for testing, never overwritten.

## Intended behavior

- Download/import installs a mod with its switch off. Download and Enable are separate actions.
- Main-menu Enable and in-game Settings use the same saved selection and dependency checks.
- Enabling a mod when Load mods is off enables that mod and its declared requirements. Earlier selections do not reappear.
- Optional stock replacements can switch off in the running zoo. The engine rebuilds resources around a private checkpoint, preserving the zoo; no process restart is needed.
- A pack supplying the current custom map stays on until that zoo is left. Unique custom content already placed in a zoo also prevents its provider being switched off.
- Test Zoo and Play with it use separate temporary profiles containing the selected mod and its declared requirements. End test/Stop removes those profiles and leaves normal switches and game preferences unchanged.

## Verification

| Check | Evidence |
| --- | --- |
| Downloaded mods remain available with loading off | Settings lists all 24 installed mods, five per page, while none load. |
| Clean pagination layout | Compact left/right arrows surround Page 1 of 5 through Page 5 of 5. All pages are reachable; first/last boundary clicks stay on the same page. The list, Settings buttons, page controls and help footer have separate space. The Required label and its explanation remain readable. |
| Temporary animal test | Amber Mossbun appears in the animal catalogue only during its temporary test; End test removes it again. |
| Temporary settings isolation | Normal mods.cfg, zoo.ini and Something.zt1save hashes stay unchanged across temporary tests. |
| Live optional replacement | Moonstone Zoo Theme switches off/on/off in Something.zt1save; grass, walls and shelter change, cash stays at -4130, and pause is preserved. Engine PID remains 11168. |
| Zoo preservation | Each successful in-game reload verifies cash and animal, staff, guest, object, fence and exhibit counts against its private checkpoint. |
| Main-menu Enable | Starting with Load mods off, Enable selects only Fastsheep, changes its label to Disable, and leaves the other installed mods off. |
| Bundle selection | Moonstone-Zoo-Theme enables as one self-contained pack; its three custom maps appear. |
| Current map requirement | Opening Moonstone Habitat marks its theme Required. Both the per-mod switch and Load mods refuse to remove it. The explanation says to leave the current zoo first. |
| Placed custom animal requirement | A new concrete exhibit on stock Basic Grass contains one Amber Mossbun. Both its individual switch and the master switch refuse removal and name Amber Mossbun 1 as the reason. |
| Requirement released | After returning to the main menu, the same theme can be disabled. |
| Normal Lab switch reaches Play | PlasmaFence is enabled and disabled through the Lab's persistent switch while Something.zt1save runs in normal Play; native reload completes in the same process. |
| Private profile cleanup | No mod-test directories remain after End test and Stop. |
| Local regression checks | All 13 mod-profile tests pass; engine builds without errors; git diff whitespace check passes. |

Native screenshots are stored in `E:/zt1-shots/mod-switches-2026-10-27/`, including `mod-menu-enable.png`, `something-theme-off.png`, `custom-map-required-reason.png`, and `map-lock-released.png`. The cleaned pagination is shown in `ui-page-1-panel.png`, `ui-page-3-required-panel.png`, and `ui-page-5-panel.png`.

## Player data preservation

Normal mods.cfg is restored to `loadMods=0`, with no enabled entries. All 24 installed mods remain on disk.

The original Something.zt1save SHA-256 remains:

`19C664DB6A8876752B91BA7FE9EFFE64DE16A01C2B0DF5CE9E9A5F27F3573D97`

No release or remote publication is part of these checks. This is targeted activation/dependency verification, not a compatibility certification for every installed mod.
