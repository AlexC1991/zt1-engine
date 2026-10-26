# Swimming and marine mod tutorial

Recorded 26 October 2026. This extends the three external-import lessons with two water cases in Wildlife Import Tutorial. These are custom variants made with the studio's Remake and Recolour controls, using the game's hippo and dolphin animations. They are not two additional original PNG/OBJ animals or complete new animation libraries. Only the main swimming animation is recoloured; inherited land, resting, surface and other behavior art can retain its original appearance.

| Variant | Kind and base | Saved custom properties | Main custom art |
|---|---|---|---|
| Copperbank Hippo (copperba) | Zoo animal, Hippopotamus | Price 1350; hunger increment 7; group minimum 2 | swim: 14 frames, five directions, copper recolour |
| Azure Reef Dolphin (azureree) | Marine animal, Bottlenose Dolphin | Price 1450; hunger increment 5; preferred depth 3–6; minimum adoption depth 2; marine specialist 9552 | walk: 10 underwater swim frames, five directions, azure recolour |

## Create the water variants

1. Open the tutorial mod. Wait for Engine running and the base-species list to load. New animal → Zoo animal → Copperbank Hippo → Hippopotamus. Confirm the selected preset before Make it. If the marine list shows only Plains Zebra, cancel and let the catalog reconnect; do not continue with that fallback.
2. In Animations, Draw beside swim. In the stencil browser choose Remake. This copies the real animation in every direction and frame. Recolour from #888080 toward #b96d37 with closeness 45, then Save. Preserve the separate land, water-idle, surface, submerged and shadow slots.
3. Change the price and hunger increment in the inspector. Let each save finish before changing another field.
4. New animal → Marine animal → Azure Reef Dolphin → Bottlenose Dolphin. Draw beside walk → Remake. Recolour from #808080 toward #268fbe, closeness 60, then Save. Set the custom price, hunger increment and preferred depth range.
5. Review Needs: this dolphin uses a Marine Specialist, fish, salt water and marine behavior. The hippo uses a Zookeeper, grass chow and a mixture of fresh water, savannah and dirt. Swims and Lives under water are different properties; use a suitable base rather than changing only the kind label.
6. The dolphin preset's shadowidle link points at a missing source path. Use Link beside shadowidle and replace it with animals/dolphin/m/ssubswim/ssubswim. The engine already has an alias for the stock species; this explicit link also lets the creator's check resolve the mod. Keep underwater shadows and surface underlays aligned with the corresponding animation frame counts.

## Build and test their homes

Use Test Zoo on a flat map and pause while building. Make a chain-link exhibit with fresh water and a dry bank. The hippo prefers about 70% fresh water, 15% savannah and 15% dirt. Water reeds are placed on land beside the pond, not on water tiles. Keepers are placed outside the enclosure and enter through its gate.

Make a separate completed rectangle of tank walls. A completed tank fills with salt water. Add a filter on an adjacent outside tile, kelp and ocean floor rocks inside, and a marine specialist outside. In this run the tank had 288 tiles. Wall height and water depth are not interchangeable after adjustment: a five-unit wall gave four units of water depth.

Place two hippos, one initially on land and one in fresh water, and a male and female dolphin in the tank. Run naturally and follow the animals. Do not treat a forced animation preview as proof of natural swimming. The land-spawned hippo entered water within the first minute and was back on land by three minutes. The dolphin trace selected surface and submerged targets between two and four height units.

For feeding stress, set Hunger to 85 in the animal inspector and let the normal carers and behaviors run. The zookeeper supplied grass chow and the marine specialist supplied fish. A hippo briefly reached hunger 100 before recovering, so do not stop after the first measurement. Actual mouthful steps are saved in feeding-steps.json.

The following are deliberate Test Zoo stress controls, not steps for authoring an animal or ordinary player requirements. The exhibit number is required:

- dirty 2 30: set tank 2 purity to 30. After ten simulated seconds its running filter raised purity to 40, suitability fell from 73 to 64, and the animal requested cleaner water.
- fill 2 fresh: the salt-water dolphin's suitability fell to 23 and it requested different salinity. Restore with fill 2 salt.
- tankwalls 2 2 then fill 2 salt: actual water depth is one. A new dolphin was rejected with the deeper-tank message. Restore tankwalls 2 5 and fill 2 salt.
- Try a dolphin in the dry land exhibit: the game rejects it and requests a tank with water.

Restore good conditions after negative checks. Final sampled hippos had suitability 78 and happiness 100; the dolphins had suitability 72 and happiness 95–100. No sampled animal was sick or escaped. These are functional fixtures with remaining habitat recommendations, not perfectly balanced finished exhibits.

## Verify in ordinary Play

Save as a map → Aquatic Mod Test → Into Wildlife Import Tutorial. This saves the habitat design; it does not carry the live animals and staff into a new freeform game. In Play, choose Play Freeform Game, scroll to Aquatic Mod Test and start it. Open Creatures, select Mods, select the animal icon, then select the male or female button before clicking the habitat. Selecting an icon alone only changes the information panel.

Copperbank Hippo cost 1350 and Azure Reef Dolphin cost 1450 in the normal adoption panel. Both sexes were adopted into the appropriate homes. Native screenshots show the copper swim art and blue underwater dolphin art. Other poses still use inherited art. Native-swimming.mp4 is actual game capture at two frames per second, silent; native-frame-times.json records capture times. It plays for sixteen seconds and is a low-frame-rate evidence clip.

## What passed, and what remains

| Check | Result |
|---|---|
| Custom zoo swimmer | Natural entry, swimming/water idle, exit and land behavior observed |
| Custom marine mod | Accepted in tank, routes within tank, surface/submerged targets observed |
| Wrong habitat | Land placement and insufficient depth rejected |
| Custom needs | Correct prices, hunger rates, carer and food loaded; salinity and purity changed suitability |
| Filtering | Running filter increased purity 30 → 40 over ten seconds |
| Feeding | Normal carers supplied food; mouthful events and hunger recovery observed |
| Ordinary Play | Habitat map loaded and both sexes of both variants adopted and rendered |
| Local package rules | Passed: 19 files, 2.2 MB including authoring/work files; builder Problems 0 |
| Hippo engine diagnostics | 0 problems, 147 animations, 85 behavior sets |
| Dolphin engine diagnostics | 10 warnings remain, documented below |

The dolphin retains six unsupported fendtrick steps and an undefined stand in bcgland, both present in the stock dolphin. Its surface_hoop_jump has 20 frames against a 26-frame shadow: stock adults have the same mismatch. The creator's young fallback also uses that adult mapping, producing a third mismatch. The stock diagnostic had 23 missing young-art paths; the new variant's inherited adult fallback has none. The marine mod therefore passes basic movement, placement and care tests but is not certified for shows, hoop tricks, juvenile visuals, breeding balance or every Marine Mania behavior.

The raw Lab recording includes retakes and waits and does not fully capture the embedded native game. Separate native screenshots and the native clip supply that evidence. Failed form selectors, an unloaded preset list and the first malformed dirty command are retained in steps.json with verified:false. Some scene placements failed and were corrected; only successful placements are used for conclusions. A cached studio map/file count needed a reload; the map itself was already present in ordinary Play. No engine source changes or builds were needed for these cases. No public upload, commit or push was performed.

See report.md for the compact results, events.json for logged checks, steps.json for original checkpoint times, and walkthrough.mp4 for the captioned tutorial.
