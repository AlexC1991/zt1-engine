# Natural light: evidence and mapping policy

The optional day/night renderer must distinguish visible bioluminescence (light produced by an organism) from fluorescence (re-emission of incident light), ordinary reflected colour and laboratory fossil imaging. A species receives an emitted-light mapping only when a source establishes the exact species, emitting body region and relevant conditions. A light-producing relative is not sufficient. Infrared body heat is outside this visible-light feature.

The initial evidence records below are followed by the 2 November species/fixture review. No new biological emitter has been enabled without matching species-level evidence.

## Native identity checks

Read from the owner's installed Marine Mania `animalsa.ztd` definitions and the corresponding strings in `lang200.dll`, without modifying or distributing either file:

| Resource | cNameID / cGenus | Native identity | Current decision |
|---|---|---|---|
| animals/jellyfsh.ai | 5403 / 5162 | Lion's Mane Jelly | Species-specific primary evidence still unresolved; do not infer emission from other jellyfish. |
| animals/gsquid.ai | 5421 / 5180 | Giant Squid | Do not apply a generic squid glow; species-specific positive evidence is pending. |
| animals/octopus.ai | 5415 / 5174 | Pacific Octopus / Giant Pacific Octopus | Do not borrow photophores from unrelated deep-sea octopuses; no emitter mapped. |
| animals/morayeel.ai | 5412 / 5171 | Green Moray Eel | Fluorescence evidence exists; do not turn that into an autonomous night lamp. |

## Research records

- MBARI explains the biochemical production of light and separately studies fluorescent lures. This supports keeping emitted light and excitation-dependent fluorescence as different renderer properties. [MBARI research project](https://www.mbari.org/project/bioluminescence-and-fluorescence/).
- The original research paper *Discovery and Characterization of a Bilirubin Inducible Green Fluorescent Protein From the Moray Eel Gymnothorax zonipectis* includes green-moray biofluorescence observations. Its main protein characterization is for **G. zonipectis**, not permission to assign its measured spectrum wholesale to every moray. Excitation/fluorescence must remain separate from an unlit animal producing light. [Frontiers in Marine Science, 2021](https://www.frontiersin.org/journals/marine-science/articles/10.3389/fmars.2021.678571/full).
- Experimental evidence confirms luminous sharks in particular taxa, including **Zameus squamulosus**. It does not establish that every shark in the game emits light. Leave the game's individual shark species unlit until their own evidence is recorded. [Duchatelet et al., 2021](https://pubmed.ncbi.nlm.nih.gov/33529364/).
- *Laser-Stimulated Fluorescence in Paleontology* illuminates fossils with an external laser to reveal material and structures. **Implementation inference:** those observations do not establish spontaneous visible light in living dinosaurs. No dinosaur bioluminescence is inferred or enabled from fossil fluorescence. [Kaye et al., 2015](https://pmc.ncbi.nlm.nih.gov/articles/PMC4446324/).
- Genuine light-producing species also require husbandry/trigger checks. The AZA jellyfish care manual describes crystal-jelly bioluminescence as dependent on an appropriate dietary light-emitting compound. That is a different animal from the game's lion's mane jelly. Do not apply its identity or conditions to the native jellyfish. [AZA Jellyfish Care Manual, 2021](https://assets.speakcdn.com/assets/2332/jellyfish_care_manual_second_edition_2021_final.pdf).

## Existing electric-fence effect

Native data confirms the Tyrannosaurus and Velociraptor `bTestFence` behavior uses `fPlayWithSound(test_fence,zap)`, with `test_fence = fencezap`. Guests use their `fencezap` animation when the existing live-electric-fence test succeeds (`Guests.cpp`, copied from zoo.exe 0x425af4; animal test 0x438ebc). A future cosmetic light mapping should follow these actual playing effects, not make all fences continuously glow or alter shock chances, happiness penalties or behavior. The native shock/camera light enhancement is implemented and checked; it does not change the existing gameplay shock rules.


## Species and fixture review - 2 November 2026

`DAY_NIGHT_EMISSION_DECISIONS.csv` records all 21 biological holds, the green moray and the buoy separately. The native `.ai` identities and English name/help strings were read from the local game. The marine foliage/help entries only supply common labels: they do not name a luminous species or specify emitting tissue. An unresolved species is a reason to keep emission off, not a claim that every member of its group is non-luminous.

The species-level inventory by Claes, Haddock, Coubris and Mallefet (2024) distinguishes confirmed, potential and unknown reports and includes original unpublished observations: [paper and supplementary table descriptions](https://pmc.ncbi.nlm.nih.gov/articles/PMC11051050/). The separate Duchatelet and Dupont functional dataset was downloaded from its publisher and inspected locally: [article](https://link.springer.com/article/10.1007/s42995-024-00250-0), [supplementary XLSX](https://static-content.springer.com/esm/art%3A10.1007%2Fs42995-024-00250-0/MediaObjects/42995_2024_250_MOESM1_ESM.xlsx). Its positive-species listing has no Cyanea, Architeuthis or Enteroctopus entry. Absence from a positive list is not experimental proof of absence.

### Native adoptable animals

- **Giant squid:** Roper and Jereb's FAO Architeuthidae account, printed page 121, gives apparent absence of photophores. Leave it unlit; unrelated squid light organs are not transferable evidence. [FAO Cephalopods of the World, volume 2](https://www.fao.org/docrep/014/i1920e/i1920e.pdf).
- **Lion's mane jelly:** The native identity is Cyanea, not the well-studied crystal jelly or mauve stinger. No matching positive observation with tissue/trigger details was verified. Its emitter remains off and this evidence gap stays explicit.
- **Pacific octopus:** The directly observed blue-green sucker light belongs to the deep-sea Stauroteuthis syrtensis. That is a different animal from the native Pacific octopus. Do not assign those sucker lights to it. [Johnsen et al., 1999, primary study](https://www.journals.uchicago.edu/doi/10.2307/1542994).
- **Green moray:** The primary work includes native-species biofluorescence observations, but the characterized protein is from a different moray. External excitation is necessary for fluorescence; ordinary night lighting must not make it a self-powered lamp. [Guarnaccia et al., 2021](https://www.frontiersin.org/journals/marine-science/articles/10.3389/fmars.2021.678571/full).
- **Dinosaurs:** Laser-stimulated fossil fluorescence is an imaging method. It is not evidence that the living animal emitted visible light. No natural dinosaur emission map is justified by it. This is an implementation inference, not proof that extinct animals could never possess such biology. [Kaye et al., 2015, primary method paper](https://doi.org/10.1371/journal.pone.0125923).

### Native marine scenery

Brittle stars, sea stars and sea cucumbers include luminous species, but their native labels do not identify them. A direct brittle-star experiment demonstrates why the distinction matters: luminous Amphiura filiformis was compared with non-luminous A. chiajei. The native Brittle Sea Star cannot be silently assigned either identity. [Coubris et al., 2024, primary study](https://pmc.ncbi.nlm.nih.gov/articles/PMC10927081/).

These marine objects have no autonomous emitter: Sea Anemone, Barnacles, Clam Bed, Orange Cup Coral, Divercate Tree Coral, Feather Duster Worm, Fire Coral, Red Gorgonian, Sand Dollar, Sea Cucumber, Sea Star, Sea Sponge, Stove Pipe Sponge, Tube Worm and Purple Sea Urchin. The later excited-material pass below adds separate reference fluorescence to Sea Anemone and Divercate Tree Coral. Neither bright colour nor group-level luminous examples establishes an emitter for these generic assets. Medium/Large Coral Formation also supplies no identities for its constituent organisms. Fluorescent coral images require excitation and do not authorize a perpetual night glow.

The primary sponge discovery is an undescribed deep-sea Cladorhizidae, not a generic reef or stove-pipe sponge. It also discusses uncertainties around older Suberites claims. This is a concrete reason not to generalize a positive sponge record to the native objects. [Bessho-Uehara et al., primary study](https://www.frontiersin.org/journals/marine-science/articles/10.3389/fmars.2020.576476/full).

Future positive species support needs a confirmed native identity, body-region mapping, emission spectrum/colour and trigger. Diet, age and stimulation can affect light production; sunset alone is not a universal biological switch. No animal needs, husbandry or behavior was altered by this review. The restrained water reflection is a cosmetic material response, not a claim of luminous plankton in every tank.

### Buoy fitting

The local review includes all 14 idle frames in four views (56 images). Its native definition supplies a generic Buoy label and bobbing idle animation, without a light state or sound/flash rule. Maritime guidance explicitly includes both lighted and unlighted buoys, and reflective markings do not themselves emit. The small top fitting remains insufficiently identified for an automatic native beacon map. Leave it unlit; a designed beacon could later be offered as an explicit addition. [Canadian Coast Guard navigation guidance](https://www.canada.ca/en/canadian-coast-guard/corporate/publications/canadian-aids-navigation-system-2023.html).

Local raw native definitions/English strings, the downloaded XLSX and the buoy contact sheet stay in `E:/zt1-shots/day-night-v8-2026-11-02`. They are research evidence and are not distribution assets. The blocked Claes supplementary ZIP was not acquired; its contents have not been represented as inspected.


## Excited material pass - 2 November 2026

The green moray now has a green tissue response to incoming blue-weighted light. It is drawn with the current native body pose inside the animal's existing tank clip. It is not a point-light emitter. Red light and darkness do not energize it; solid obstructions and projector direction also gate excitation. The runtime uses RGB rather than a spectral renderer. Its exposure, tint and daylight contribution are graphical parameters, not measurements of G. funebris.

Two generic marine objects, Sea Anemone and Divercate Tree Coral, also receive explicit reference fluorescence materials. The primary Anthozoa work identifies fluorescent proteins in non-bioluminescent species, including Anemonia majano: [Matz et al., 1999](https://pubmed.ncbi.nlm.nih.gov/10504696/). A Dendronephthya protein structure study documents green fluorescence and UV-driven photoconversion: [primary structure study](https://journals.iucr.org/d/issues/2016/08/00/mn5111/mn5111.pdf). The native game supplies no exact Latin identity for either object. These two mappings are optional graphical reference approximations, not confirmed native-species biology. No photoconversion, diet or husbandry change is implemented.

The tissue masks exclude transparent pixels, retained native shadow pixels and low-colour stony regions. Fluorescence is bounded to 64 visible material cues. It creates no secondary raycast lamp, does not make all marine objects glow, and remains under the day/night switch. Fire coral and other unresolved objects remain ordinary materials. The earlier no-autonomous-emitter decisions still apply; the decision CSV now records the separate excited material support.
