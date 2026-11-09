#ifndef GRAPHICS_QUALITY_HPP
#define GRAPHICS_QUALITY_HPP

#include <cstdint>
#include <string>

// addition: the graphics quality settings, as a modern PC game's graphics
// page has them - a preset (Low, Medium, High, Ultra, or Custom when the
// settings under it are set one by one) and the day and night lighting's own
// detail (Features::dayNightLighting, itself a switch: off, the original's
// constant daylight and none of this). High is the lighting as it always
// was; the others trade its detail for speed (a laptop's or an APU's) or
// spend more on it. Kept in zoo.ini [user]: graphicspreset, shadowquality,
// shadowrange, creatureshadows, lampquality, lightingdevice. The first time
// the game starts on a PC (no graphicspreset yet) the preset is the one
// recommend() picks for its graphics: Low for integrated graphics (a
// laptop's, an APU's), High for a graphics card.
namespace GraphicsQuality {

enum Preset { Low, Medium, High, Ultra, Custom };

struct Settings {
  int preset = High;
  int shadows = 2;   // the sun's shadows: 0 low (512, hard edges), 1 medium (768), 2 high (1024), 3 ultra (1536)
  int range = 2;     // how many shapes near the view are lit in 3D: 0 near, 1 medium, 2 far, 3 farthest
  int creatures = 2; // animals', guests' and keepers' light: 0 off, 1 simple (their shadows only), 2 full (3D)
  int lamps = 2;     // lamps' light: 0 low, 1 medium, 2 high (the detail of their pools of light, how many count)
  int device = 0;    // where the lighting is worked out: 0 the graphics card where it can, 1 the processor
};
const Settings &settings();
// Kept in zoo.ini; the preset follows from the rest (Custom when they match
// none of the presets)
void set(const Settings &s);
// A preset's settings (where the lighting runs kept from s)
Settings preset(int which, const Settings &s);
int presetOf(const Settings &s);
// From zoo.ini; the first time, the recommended preset (and it's kept)
void load();
// ours: a preset for this run only, not kept - the check modes' pictures
// and the benchmarks at High, whatever the PC (ZT_GRAPHICS_PRESET=low,
// medium, high or ultra picks another for a benchmark)
void forThisRun(int which);

// The preset for this PC's graphics, and why (its name and memory)
int recommend(std::string *why = nullptr);

// Changes with every setting that changes the lighting (the lighting is
// made again when it does)
uint64_t serial();

// What the lighting uses
int sunMapSize();        // the sun's shadow maps' largest size (texels across)
float sunTexelsPerTile(); // and how many texels a map tile has in them (High: 8)
bool softShadows();      // their edges spread over a few texels (5 taps) or hard (1)
int shapeFaceBudget();   // how many shape faces near the view are lit in 3D
int creatureLight();     // 0 none, 1 their shadows only, 2 lit in 3D as well
float lampDetail();      // how finely a lamp's pool of light is sampled (1: as always)
int lampLimit();         // how many lamps count
bool lightingOnGraphicsCard();

} // namespace GraphicsQuality

#endif // GRAPHICS_QUALITY_HPP
