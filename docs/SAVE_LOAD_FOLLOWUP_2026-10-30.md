# Save-load follow-up, 30 October 2026

The owner installed candidate 2026.10.29 on the laptop. The first attempts at
loading both a personal zoo and Marine-Show-Test.zoo left the main menu frozen,
then white, with audio continuing, before the application closed. Retesting the
save subsequently worked. The supplied startup log confirms the installed
2026.10.29 executable and Toon mod, but contains no failing-load trace.
The intermittent closure has not been reproduced or established as fixed.

## Confirmed code defect and local correction

The menu starts a background worker that loads ItemCatalog and Research.
gameLoad copied Research::branches() before waiting for Research::load(). The
worker can still be modifying that vector. The loader now calls both mutex-
protected load methods before taking the rollback snapshot or resetting research.
It also logs the start and elapsed map, interface, decode and activation stages.
This removes that unsafe ordering; it does not establish the cause of the
reported white-window closure. Map/interface loading still runs synchronously.

The Lab build passes with no reported compiler problems (129.5 seconds).
Local engine and Play copy SHA-256:
fc20d141d53b37c49ec5203871b8688e0965a11d0307bb8ba6549592cc6da343.

## Runtime checks through Lab MCP

- Fresh engine launch, ordinary main-menu Load Saved Game, supplied shared zoo:
  loaded in 2,376 ms, with the zoo visible.
- Ordinary in-game Load Game, retained engine-only-filter-28.zt1save:
  loaded in 1,505 ms.
- Separate isolated Toon test profile, fresh launch, supplied shared zoo:
  loaded in 2,418 ms. The log confirms Toon and two shader definitions enabled.
- Same isolated profile, original-game resave wet-dung-fixed-original-resave-201.zoo:
  loaded in 1,362 ms, restoring two animals, six staff and 50 guests, zero missing.
- A theme save requiring absent aquaticmodtest.scn was rejected with a named
  missing-map message; the current zoo remained open.

These timings measure the load routine after file selection, not total startup.
They do not simulate the laptop's GPU or guarantee that every cold preload race
was exercised. No test writes over the owner's personal saves.

Evidence is under E:/zt1-shots/save-compatibility-2026-10-29/:
load-race-fix-cold-01.png, load-race-fix-original-toon.png and
load-race-fix-toon.log. The isolated profile is stopped after testing, with normal
mod configuration unchanged (SHA-256
ab082d5ee423d0eec7a365eeacb5d59a6e301992ea7be2279936e63d956ae256).

## Distribution state at the time of the follow-up

This local correction is not yet in the existing CodeRook save-23 installers.
Those remain the previously inventoried candidate with engine SHA-256 73011c28...
No public release, update feed, GitHub push or CodeRook source save is made by
this follow-up. Repackage and verify matching artifacts before distributing the
correction. Keep the intermittent load/freeze report open for owner testing.

The CodeRook Lab object download is the complete Inno installer, but its generic
object URL supplies no installer filename. Save or rename it to
ZT1-Lab-Setup-2026.10.29.exe. Expected size is 616,266,042 bytes, SHA-256
70619364cedf9dff65762ff94b6e8b35586a17e4110237b6fe44444e11b663fa.
The older attachment route truncated this large file; do not substitute it for
the verified object route. Filename delivery remains a CodeRook download issue,
not evidence that the uploaded payload is a different kind of file.

## Release authorization

The owner subsequently approved publishing all current changes as 2026.10.30,
including this loading-order correction, and explicitly requested no further
testing. The game installer, portable game and Lab installer are rebuilt together
for that version. Release notes retain the intermittent closure as a known issue
and separate new features, bug fixes and known issues.
