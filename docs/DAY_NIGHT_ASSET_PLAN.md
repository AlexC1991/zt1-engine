# Native asset lighting and shadow plan

Audit date: 2 November 2026. This is a classification and authoring plan, not a claim that all mappings are implemented. No new runtime lighting profiles or replacement artwork were added during this audit.

The inventory covers 676 current catalogue entries plus 26 runtime definitions: four guest types, 18 terrain types and four runtime effects. The DRT base occurs twice in raw metadata and is deduplicated here. There are 702 distinct definitions, with 13,185 exported native-art views. The only definition without art is a local test entity; the waterfall terrain slot resolves to native test-texture art. Neither is treated as a working lit asset.

| Treatment | Definitions | Work required |
|---|---:|---|
| Ordinary shadows | 502 | Reuse cached shape/pose shadows; no emission map. |
| Light map + shadows | 94 | Map only the listed light-bearing regions; preserve structural shadows. |
| Brief effect map + body shadows | 12 | Map transient native flash/spark pixels; normal body remains non-emissive. |
| Special material + appropriate shadows | 35 | Separate water/glass/steam/ice or fluorescence response from solid geometry. |
| Receiver / real surface geometry | 35 | Receive light on actual surfaces; only real elevation casts. |
| Species evidence unresolved; emission off | 21 | Emission disabled until exact species, region and trigger are evidenced. |
| Material identity unresolved; emission off | 1 | Buoy fitting identity/timing unresolved; no beacon enabled. |
| Excluded / missing or placeholder art | 2 | One local missing-art test item and one native terrain placeholder. |

**106 definitions are in the emission scope:** 94 light-bearing designs and 12 actor effect mappings. Five of those definitions already have accepted preview emission profiles. Another three accepted preview profiles cover water or an explicitly broken lamp. There are still only eight authored preview profiles in the renderer. A planned row is not a completed mapping.

## What the labels mean

Every ordinary world surface still receives scene light. "Ordinary shadows" means no separate emitted-light mask is needed, not that the surface ignores lamps or the sun. Light-bearing objects also cast shadows through their solid parts; flames, water and glass need separate handling. Ground textures receive shadows, while actual slopes, cliffs and raised decks can cast them.

Building window/interior illumination and selected advertising/sign backlighting are proposed optional night designs grounded in existing visible regions. Daylight art does not prove that every window or sign was electrically lit in the original game. Painted signs/statues, roof vents, beam ends, gold trim, white fur and metal glints are not automatically emitters. The CSV records those exclusions per item.

## Full inventory and component scope

`DAY_NIGHT_COVERAGE.csv` is the authoritative per-definition list: native resource, display name, category, emission decision, casting/receiving, exact target regions, exclusions, state conditions, review scope and remaining checks. `DAY_NIGHT_COMPONENTS.csv` adds native animal egg/box/dust references and the shared tank/deck/effect/UI rules. Child parts are not counted as new catalogue entries.

Local review gallery: `E:/zt1-shots/day-night-asset-audit-2026-11-02/index.html`. Source images and derived contact sheets stay local on E: and must not be redistributed. The text inventory contains no original art.

## Light-bearing designs

The following list is the authoring scope. Listed solid structure still uses shadows. Only the named regions emit. Exact masks, intensities and final visual approval are later work.

| Item / resource | Regions to map | State |
|---|---|---|
| Santa Claus â€” `ambient/santa.ai` | Native golden sparkle trail; tiny red nose only if the actual sprite region supports it. | Effect pixels only while Santa is present; no ground-height light from a flying trail. |
| Cotton Candy Stand â€” `scenery/building/afhut.ai` | Open service hatch interior. | Dusk/night; preserve the original animation and silhouettes. |
| Aquatic Restroom â€” `scenery/building/aqbath.ai` | Glazed high windows/roof clerestory and a visible open doorway interior. | Dusk/night; preserve the original animation and silhouettes. |
| Aquatic Gift Shop â€” `scenery/building/aqgftshp.ai` | Glazed round window bands/portholes and entry interior. | Dusk/night; preserve the original animation and silhouettes. |
| Lobby's Restaurant â€” `scenery/building/aqrestra.ai` | Framed dining windows and entry interior. | Dusk/night; preserve the original animation and silhouettes. |
| Arctic Gift Shop â€” `scenery/building/arcshop.ai` | Visible warm round and rectangular windows; doorway interior. | Dusk/night; preserve the original animation and silhouettes. |
| Nocturnal House â€” `scenery/building/asinochs.ai` | Purple/blue enclosed display windows. | Low-intensity nocturnal exhibit window light; no bright warm wash across the whole house. |
| Animatronic Theater â€” `scenery/building/asitheat.ai` | Existing small perimeter floor fixtures and enclosed entrance/window regions. | Fixed perimeter fixtures at dusk; roof dinosaur motion must not alter the light map. |
| Atlantean Gift Shop â€” `scenery/building/atlshop.ai` | Existing orange circular windows and entry light. | Dusk/night; preserve the original animation and silhouettes. |
| Aviary â€” `scenery/building/aviary.ai` | Enclosed house windows and entrance interior; wire enclosures remain non-emissive. | Dusk/night; preserve the original animation and silhouettes. |
| Restroom â€” `scenery/building/bathroom.ai` | Glazed high windows/roof clerestory and a visible open doorway interior. | Dusk/night; preserve the original animation and silhouettes. |
| Burger Stand â€” `scenery/building/bgrstnd.ai` | Front advertising sign/model and service-window interior; tiny spill from the hatch. | Sign steady at dusk/night; service hatch while operating; cooking steam follows the active art. |
| Boat Rental Shack â€” `scenery/building/boatshak.ai` | Existing round/clerestory windows and the occupied service/entrance interior. | Dusk/night; preserve the original animation and silhouettes. |
| Bronto Burger Stand â€” `scenery/building/brontbrg.ai` | Front service window and the food-shaped advertising face. | Dusk/night; preserve the original animation and silhouettes. |
| Campfire â€” `scenery/building/campfire.ai` | Native animated flames and a small nearby warm spill. | Only lit flame pixels emit; fire is non-solid; surrounding stones still cast/receive shadows. |
| Carousel â€” `scenery/building/carousal.ai` | Individual canopy bulbs and platform trim. | Steady bulb map at night, anchored to fixed canopy/reference art; ride motion must not flicker the bulbs. |
| Swim Shack â€” `scenery/building/chroom.ai` | Existing round/clerestory windows and the occupied service/entrance interior. | Dusk/night; preserve the original animation and silhouettes. |
| Dinosaur Cinema â€” `scenery/building/cinema.ai` | Entrance marquee bulbs, sign faces and enclosed ticket/doorway/window interior. | Dusk/night; preserve the original animation and silhouettes. |
| Compost Building â€” `scenery/building/compost.ai` | Visible pole-top globe fixtures; enclosed window panes if exposed. | Dusk/night; preserve the original animation and silhouettes. |
| Crustacean House â€” `scenery/building/crabhous.ai` | Enclosed window bands/glass exhibit interior. | Dusk/night; preserve the original animation and silhouettes. |
| Fancy Burger Stand â€” `scenery/building/dbrgstnd.ai` | Front serving hatch and advertising emblem/model. | Dusk/night; preserve the original animation and silhouettes. |
| Fancy Drink Stand â€” `scenery/building/ddnkstnd.ai` | Front serving hatch and advertising emblem/model. | Dusk/night; preserve the original animation and silhouettes. |
| Fancy Hot Dog Stand â€” `scenery/building/ddogstnd.ai` | Front serving hatch and advertising emblem/model. | Dusk/night; preserve the original animation and silhouettes. |
| Fancy Family Bathroom â€” `scenery/building/dfbthrom.ai` | Glazed high windows/roof clerestory and a visible open doorway interior. | Dusk/night; preserve the original animation and silhouettes. |
| Fancy Ice Cream Stand â€” `scenery/building/dicecstd.ai` | Front serving hatch and advertising emblem/model. | Dusk/night; preserve the original animation and silhouettes. |
| Dinosaur Gift Shop â€” `scenery/building/dinogift.ai` | Hanging lantern glass cores, visitor glazing and shop doorway. | Dusk/night; preserve the original animation and silhouettes. |
| Stego Putt â€” `scenery/building/dinoputt.ai` | Enclosed ticket hut windows/serving hatch. | Dusk/night; preserve the original animation and silhouettes. |
| Kids Arcade â€” `scenery/building/dkarcade.ai` | Arcade video screen/display, enclosed shop windows and entrance interior. | Screen light follows active screen art; windows/entrance get a separate steady night map. |
| Kids Dino Gift Shop â€” `scenery/building/dkgifts.ai` | Real shop window openings and entrance interior. | Dusk/night; preserve the original animation and silhouettes. |
| Swim with Dolphins â€” `scenery/building/dolshack.ai` | Existing round/clerestory windows and the occupied service/entrance interior. | Dusk/night; preserve the original animation and silhouettes. |
| Fancy Pizza Stand â€” `scenery/building/dpzzastd.ai` | Front serving hatch and advertising emblem/model. | Dusk/night; preserve the original animation and silhouettes. |
| Drink Stand â€” `scenery/building/drkstnd.ai` | Front advertising sign/model and service-window interior; tiny spill from the hatch. | Sign steady at dusk/night; service hatch while operating; cooking steam follows the active art. |
| Small Elephant Shelter â€” `scenery/building/elehous1.ai` | Actual framed glass windows only; low-level interior light. | Dusk/night; preserve the original animation and silhouettes. |
| Large Elephant Shelter â€” `scenery/building/elehous2.ai` | Actual framed glass windows only; low-level interior light. | Dusk/night; preserve the original animation and silhouettes. |
| Family Restroom â€” `scenery/building/fbatroom.ai` | Glazed high windows/roof clerestory and a visible open doorway interior. | Dusk/night; preserve the original animation and silhouettes. |
| Tropical Aquarium â€” `scenery/building/fishhous.ai` | Enclosed window bands/glass exhibit interior. | Dusk/night; preserve the original animation and silhouettes. |
| Lepospondyl House â€” `scenery/building/froghous.ai` | Enclosed window bands/glass exhibit interior. | Dusk/night; preserve the original animation and silhouettes. |
| Frozen Yogurt Stand â€” `scenery/building/fystand.ai` | Service hatch and actual glazed wall windows. | Dusk/night; preserve the original animation and silhouettes. |
| Gift Shop â€” `scenery/building/giftshp.ai` | Ground-floor glazing and entrance interior. | Dusk/night; preserve the original animation and silhouettes. |
| Small Giraffe Shelter â€” `scenery/building/girhous1.ai` | Actual framed glass windows only; low-level interior light. | Dusk/night; preserve the original animation and silhouettes. |
| Large Giraffe Shelter â€” `scenery/building/girhous2.ai` | Actual framed glass windows only; low-level interior light. | Dusk/night; preserve the original animation and silhouettes. |
| Hot Dog Stand â€” `scenery/building/hdogstnd.ai` | Front advertising sign/model and service-window interior; tiny spill from the hatch. | Sign steady at dusk/night; service hatch while operating; cooking steam follows the active art. |
| DRT Base â€” `scenery/building/helibase.ai` | Existing small pad marker fixtures and enclosed office window panes. | Dusk/night; marker points steady unless a real beacon state is identified. |
| Mammoth Cones Stand â€” `scenery/building/icecream.ai` | Front service window and the food-shaped advertising face. | Dusk/night; preserve the original animation and silhouettes. |
| Ice Cream Stand â€” `scenery/building/icecstnd.ai` | Front advertising sign/model and service-window interior; tiny spill from the hatch. | Sign steady at dusk/night; service hatch while operating; cooking steam follows the active art. |
| Insect House â€” `scenery/building/insect.ai` | Enclosed house windows and entrance interior; wire enclosures remain non-emissive. | Dusk/night; preserve the original animation and silhouettes. |
| Shaved Ice Pagoda â€” `scenery/building/japagoda.ai` | Existing pale window/infill panes on each storey and service opening. | Dusk/night; preserve the original animation and silhouettes. |
| Rainforest Restaurant â€” `scenery/building/junrest.ai` | Existing perimeter torch flames and visible service/entrance interior. | Torch light follows its visible flame state; do not turn gold carvings into neon. |
| Lava Pit â€” `scenery/building/lavapit.ai` | Animated molten lava surface and exposed hot channels. | Emission follows hot pixels through idle animation; solid rock casts shadows, liquid does not cast a solid silhouette. |
| Prairie Dog Cafe â€” `scenery/building/mexcafe.ai` | Framed windows/service hatch; nacho advertising face only on the stand. | Dusk/night; preserve the original animation and silhouettes. |
| Nacho Stand â€” `scenery/building/mexnacho.ai` | Framed windows/service hatch; nacho advertising face only on the stand. | Dusk/night; preserve the original animation and silhouettes. |
| Petting Zoo â€” `scenery/building/petzoo.ai` | Existing glazed roof panes can reveal a restrained interior at night. | Dusk/night only; no light assigned to vent caps. |
| Photo Booth â€” `scenery/building/photobth.ai` | Header/display panels; restrained enclosed booth interior at night. | Steady night sign; no exterior flash without a visible exposure cue. |
| Pizza Stand â€” `scenery/building/pizzstnd.ai` | Front advertising sign/model and service-window interior; tiny spill from the hatch. | Sign steady at dusk/night; service hatch while operating; cooking steam follows the active art. |
| Primate House â€” `scenery/building/primhous.ai` | Enclosed house windows and entrance interior; wire enclosures remain non-emissive. | Dusk/night; preserve the original animation and silhouettes. |
| Pteranodon House â€” `scenery/building/pterhous.ai` | Visitor-house windows and enclosed glazed display interior. | Dusk/night; preserve the original animation and silhouettes. |
| Reptile House â€” `scenery/building/repthous.ai` | Visitor-house windows and enclosed glazed display interior. | Dusk/night; preserve the original animation and silhouettes. |
| Restaurant â€” `scenery/building/restrant.ai` | Coffee sign, red neon awning edge and framed windows. | Steady night signs/windows; steam stays non-emissive; preserve the reviewed preview mapping. |
| Shark Tunnel â€” `scenery/building/shrkhous.ai` | Enclosed window bands/glass exhibit interior. | Dusk/night; preserve the original animation and silhouettes. |
| Stable â€” `scenery/building/stable2.ai` | Actual framed glass windows only; low-level interior light. | Dusk/night; preserve the original animation and silhouettes. |
| Large Stable â€” `scenery/building/stable3.ai` | Actual framed glass windows only; low-level interior light. | Dusk/night; preserve the original animation and silhouettes. |
| Kneemo's Sub Shop â€” `scenery/building/subshop.ai` | Front serving hatch, round porthole glazing and the sandwich advertising face. | Dusk/night; preserve the original animation and silhouettes. |
| Animal Theater â€” `scenery/building/theatre.ai` | Entrance marquee bulbs, sign faces and enclosed ticket/doorway/window interior. | Dusk/night; preserve the original animation and silhouettes. |
| Snack Machine â€” `scenery/building/vendfood.ai` | Front product/advertising panel and selector region. | Night/powered presentation; no glow from the solid coloured cabinet. |
| Drink Machine â€” `scenery/building/vendsoda.ai` | Front product/advertising panel and selector region. | Night/powered presentation; no glow from the solid coloured cabinet. |
| Small Volcano â€” `scenery/building/volcano.ai` | Idle embers/hot cracks; stronger active lava and eruption fragments. | Weak idle heat regions; eruption light only while the native used/eruption animation is playing. |
| Large Volcano â€” `scenery/building/volcano2.ai` | Idle embers/hot cracks; stronger active lava and eruption fragments. | Weak idle heat regions; eruption light only while the native used/eruption animation is playing. |
| Holiday Tree â€” `scenery/foliage/xmastree.ai` | Individual colored string bulbs and top star. | Dusk/night; preserve native animation. Tiny bulbs share a bounded nearby light, not one raycast light per bulb. |
| Tall Torch â€” `scenery/other/aftorch.ai` | Existing animated flame, warm nearby spill. | Emit only while flame visible; flame is not an opaque shadow caster. |
| Admission Booth â€” `scenery/other/aqtick1.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Admission Booth â€” `scenery/other/aqtick2.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Building â€” `scenery/other/build1.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Building â€” `scenery/other/build2.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Building â€” `scenery/other/build3.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Building â€” `scenery/other/build4.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Building â€” `scenery/other/build5.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Building â€” `scenery/other/build6.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Lava Archway â€” `scenery/other/darch1.ai` | Visible hot lava at base and hot inner channels. | Hot regions follow native animation; daylight lower contrast. |
| Dinosaur Zoo Entrance â€” `scenery/other/dgate.ai` | Four existing flame bowls at the entrance. | Emit only from visible burning flame pixels; structural parts retain shadows. |
| Dinosaur Admission Booth â€” `scenery/other/dgleft.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Dinosaur Admission Booth â€” `scenery/other/dgright.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Admission Booth â€” `scenery/other/fgate1.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Admission Booth â€” `scenery/other/fgate2.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Hotel â€” `scenery/other/hotel.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Japanese Stone Lantern â€” `scenery/other/japlant.ai` | Small lantern apertures under stone cap. | Dusk/night; small warm pool, not a luminous stone statue. |
| Rainforest Torch â€” `scenery/other/juntorch.ai` | Existing animated flame, warm nearby spill. | Emit only while flame visible; flame is not an opaque shadow caster. |
| Building â€” `scenery/other/lab1.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Lamp â€” `scenery/other/lamp.ai` | Lamp glass around the actual lantern head. | Dusk/night; intact state only. |
| Lighthouse â€” `scenery/other/lightho.ai` | Top lantern glazing around existing yellow light. | Dusk/night; steady beacon core. |
| Jack-O'-Lantern â€” `scenery/other/pumpkin.ai` | Carved eyes/mouth apertures, inner cavity. | Dusk/night; tiny candle spill. |
| Small Stable â€” `scenery/other/stable1.ai` | Existing glazed window panes and service/interior opening. | Dusk/night; restrained interior light. |
| Small Torch â€” `scenery/other/storch.ai` | Existing animated flame, warm nearby spill. | Emit only while flame visible; flame is not an opaque shadow caster. |
| Large Stone Torch â€” `scenery/other/storch2.ai` | Existing animated flame, warm nearby spill. | Emit only while flame visible; flame is not an opaque shadow caster. |
| Street light â€” `scenery/other/streetlt.ai` | Streetlamp underside; individual traffic-signal lenses. | Streetlamp dusk/night; signals only one active lens per head. |

## Brief actor effects

| Actor / resource | Region and trigger |
|---|---|
| Allosaurus â€” `animals/allo.ai` | Animated body/egg/crate shadows; native electric-shock sparks/flash need a transient emission mask. Shock only while real test_fence/fencezap effect plays; body unlit otherwise. |
| Ankylosaurus â€” `animals/ankylo.ai` | Animated body/egg/crate shadows; native electric-shock sparks/flash need a transient emission mask. Shock only while real test_fence/fencezap effect plays; body unlit otherwise. |
| Caudipteryx â€” `animals/caudipt.ai` | Animated body/egg/crate shadows; native electric-shock sparks/flash need a transient emission mask. Shock only while real test_fence/fencezap effect plays; body unlit otherwise. |
| Coelophysis â€” `animals/coelo.ai` | Animated body/egg/crate shadows; native electric-shock sparks/flash need a transient emission mask. Shock only while real test_fence/fencezap effect plays; body unlit otherwise. |
| Iguanodon â€” `animals/iguano.ai` | Animated body/egg/crate shadows; native electric-shock sparks/flash need a transient emission mask. Shock only while real test_fence/fencezap effect plays; body unlit otherwise. |
| Lambeosaurus â€” `animals/lambeo.ai` | Animated body/egg/crate shadows; native electric-shock sparks/flash need a transient emission mask. Shock only while real test_fence/fencezap effect plays; body unlit otherwise. |
| Saber-toothed Cat â€” `animals/smilo.ai` | Animated body/egg/crate shadows; native electric-shock sparks/flash need a transient emission mask. Shock only while real test_fence/fencezap effect plays; body unlit otherwise. |
| Spinosaurus â€” `animals/spino.ai` | Animated body/egg/crate shadows; native electric-shock sparks/flash need a transient emission mask. Shock only while real test_fence/fencezap effect plays; body unlit otherwise. |
| Tyrannosaurus rex â€” `animals/trex.ai` | Animated body/egg/crate shadows; native electric-shock sparks/flash need a transient emission mask. Shock only while real test_fence/fencezap effect plays; body unlit otherwise. |
| Velociraptor â€” `animals/velocira.ai` | Animated body/egg/crate shadows; native electric-shock sparks/flash need a transient emission mask. Shock only while real test_fence/fencezap effect plays; body unlit otherwise. |
| Man â€” `guests/man` | Person casts/receives; camera flash at native frame 13 (zero-based); native fencezap flashes/sparks only.  Camera pulse only on exposure; fence shock only on actual electrified-fence contact. No continuous body light. |
| Woman â€” `guests/woman` | Person casts/receives; camera flash at native frame 13 (zero-based);  Camera pulse only on exposure; fence shock only on actual electrified-fence contact. No continuous body light. |

T. rex and velociraptor full NE shock sequences show varying sparks/flash around zero-based frames 10â€“27, including dark gaps. Do not light the complete interval or interpret the shock as biological light. Guest man NE fencezap flashes are intermittent (for example 47, 49, 51 and 53); the whole 171-frame animation is not one glowing body. Adult man/woman camera flashes are visible at NE frame 13. All relevant sequences were exported in four directions for later mask alignment. The photo booth used sequence does not establish a clear exterior flash, so no repeating exterior flash is planned.

## Unresolved emission decisions

These are explicit holds, not approved glow effects. Use the listed ordinary shadow/material behavior while emission stays off. Natural light research and native identity checks are in `DAY_NIGHT_ANIMAL_EVIDENCE.md`.

| Item / resource | Missing evidence |
|---|---|
| Giant Squid â€” `animals/gsquid.ai` | Native identities checked in DAY_NIGHT_ANIMAL_EVIDENCE.md; no new emitter enabled. Biological emission remains off pending positive exact-species evidence. |
| Lion's Mane Jelly â€” `animals/jellyfsh.ai` | Native identities checked in DAY_NIGHT_ANIMAL_EVIDENCE.md; no new emitter enabled. Biological emission remains off pending positive exact-species evidence. |
| Pacific Octopus â€” `animals/octopus.ai` | Native identities checked in DAY_NIGHT_ANIMAL_EVIDENCE.md; no new emitter enabled. Biological emission remains off pending positive exact-species evidence. |
| Sea Anemone â€” `scenery/foliage/anemone.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Barnacles â€” `scenery/foliage/barnacle.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Brittle Sea Star â€” `scenery/foliage/britstar.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Clam Bed â€” `scenery/foliage/clambed.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Orange Cup Coral â€” `scenery/foliage/cupcoral.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Divercate Tree Coral â€” `scenery/foliage/dtcoral.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Feather Duster Worm â€” `scenery/foliage/fdstworm.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Fire Coral â€” `scenery/foliage/fircoral.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Red Gorgonian â€” `scenery/foliage/redgorgo.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Sand Dollar â€” `scenery/foliage/sdollar.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Sea Cucumber â€” `scenery/foliage/seacuke.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Sea Star â€” `scenery/foliage/seastar.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Sea Sponge â€” `scenery/foliage/sponge.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Stove Pipe Sponge â€” `scenery/foliage/stovpipe.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Tube Worm â€” `scenery/foliage/tubeworm.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Purple Sea Urchin â€” `scenery/foliage/urchin.ai` | Native label is not enough to transfer light production from a related marine species. Emission disabled pending species-specific evidence; fluorescence would require excitation. |
| Medium Coral Formation â€” `scenery/other/aqrock4.ai` | Rock geometry is ordinary; biological-emission review applies only to attached organisms. Emission off until species/regions/conditions are evidenced. |
| Large Coral Formation â€” `scenery/other/aqrock5.ai` | Rock geometry is ordinary; biological-emission review applies only to attached organisms. Emission off until species/regions/conditions are evidenced. |
| Buoy â€” `scenery/other/buoy.ai` | All 14 native idle frames exported; bobbing animation does not establish a flashing lens. No light until fitting identity and timing are resolved. |

Green moray fluorescence is recorded as a material response, not autonomous night light. No new animal bioluminescence is enabled. Dinosaur fossil fluorescence is not evidence of a living luminous dinosaur. Generic coral/worm/brittle-star names do not establish a particular luminous species.

## Implementation order and acceptance

1. Finish generic caster/receiver geometry for fences, tank structures, elevated decks, terrain, actors, eggs and crates. Preserve glass/mesh openings, source-shadow removal, actual height and underwater clipping.
2. Author native fixtures/fire/lava/holiday effects, then selected building windows, signs and marquee bulbs, followed by the transient shock/camera masks. Resolve the buoy/species holds before adding emission there.
3. Check four map rotations, relevant intact/used/closed/off/broken states, female/young/actor poses, shows, egg hatching, waterline placement, sunset/night transitions and return to original rendering when disabled.
4. Repeat camera-pan and crowded-zoo performance measurements against the accepted eight-item preview. Share light sources across tiny bulb strings; do not allocate one raycast light per bulb. Preserve bounded light/actor/face caches. 106 mapped definitions do not mean 106 simultaneous point lights.

The audit surveys native views and selected poses; it is not frame-by-frame visual acceptance of every animal sex/age, building state or native sprite in the archives. Missing optional animation lookups (54) include absent decay/break/extra states and are not 54 missing catalogue assets. New mods and future HD art require their own inventory and mapping review.

## Distribution

Keep renderer parameters and generic shadow/material handling in the optional engine feature. Runtime textures/meshes are derived locally from the player installation and stay in the local cache. Any replacement sprites, hand-edited raster light/normal maps or replacement meshes belong in the separate optional asset-mod project; original art and locally derived copies must not be shipped. This audit starts neither FreeRoam nor the later HD pack.


## Implementation record

The approved mapping pass is now in the renderer: 89 additional four-view light-bearing definitions, 16 additional source-free water/steam profiles, native electrical/camera effects and shared structural geometry. See DAY_NIGHT_LIGHTING.md for the checks, runtime bounds and the local video/performance evidence. The ledger statuses distinguish implemented rendering from the deliberately disabled species/buoy emission holds. Counts describe definitions and material regions, not simultaneous point lights. Full frame-by-frame visual certification of the entire original archive is outside the sampled acceptance evidence.


## Beacon follow-up

The native-frame follow-up identifies the DRT base's red roof beacon and four blue pad guide lenses in each view, plus the Dinosaur Cinema marquee and Kids Arcade roof-rail bulb sequences. The independent night fixtures use native active frames even while the building is empty. Screens remain tied to actual activity; steady panes and lettering are sampled separately. Native paint, stone, mascot bodies and rotor tips do not become lamps.

The helicopter's small powered night markers are an optional addition. Its native aircraft has no confirmed separate navigation-lamp animation, so they are not described as recovered original behavior. The ledger explicitly marks this distinction. They are renderer-coordinate attachments, with no replacement sprite assets. The four powered states are checked across all 388 native frames in four views. The local state reel is a visual test; it does not test autonomous helicopter task selection.


## Biological and fixture research outcome

The 21 biological holds, green moray and buoy now have individual source-backed decisions in `DAY_NIGHT_EMISSION_DECISIONS.csv`. No new biological emitter was justified. The giant squid has contrary photophore evidence; the moray requires excitation for fluorescence. Generic marine scenery cannot inherit a glow from an unidentified luminous relative. The lion's-mane jelly and buoy retain explicit evidence gaps. These are documented non-emitting renderer decisions, not claims that every member of a marine group is non-luminous. Source art and native biology/needs stay unchanged.


## Excitation and projecting fixtures

The follow-up adds the green moray's excitation-dependent body material and explicit reference materials for generic Sea Anemone and Divercate Tree Coral. See the evidence document for the limits of species identification. None creates an autonomous biological light source.

The lighthouse has an outward rotating cone from its lantern. Mapped building window/sign regions locate outward facade sources on their locally reconstructed vertical surfaces. Ground and surface receivers use obstructed illumination; the visible lighthouse haze stops at cached static obstructions. Sweeping does not rebuild static irradiance. Up to four projector sources are included within the normal light budget. Source rasters and generated meshes remain local; no asset mod or HD replacement has been introduced.
