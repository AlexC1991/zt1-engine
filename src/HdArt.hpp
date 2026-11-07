#ifndef HD_ART_HPP
#define HD_ART_HPP

#include <string>
#include <vector>

#include <SDL2/SDL.h>

class ResourceManager;
namespace GpuCompressed {
struct Blocks;
}

// HD art: bigger frames standing in for the original's map art (animals,
// people, fences, objects) while the map is zoomed in - at 1x the
// original's own frames are always drawn. Each frame is a PNG, a whole
// multiple of the original's size, at
//
//   <root>/<the .ani's folder>/<direction>_<frame>.png
//
// or packed (ours: a whole pack is a million frames, too many files to share
// or unpack): one archive per art folder, <root><top>/<name>.ztd - a zip,
// as the game's own .ztd files - holding <top>/<name>/... and the light
// maps of that art. A loose file wins over a packed one.
//
// The roots: each mod switched on that has an hd/ folder (a mod, so it's
// off unless the player puts it on - the Mod Menu or Game Options > Mods &
// Updates), then the game's own hd/ folder. Switching such a mod on or off
// works at once: the zoomed-in frames are made again.
//
// Making a pack (the Lab's HD art mod): exportFrames writes the original
// frames out (their clear pixels filled with their neighbours' colours, so
// an upscaler doesn't draw dark fringes), an upscaler makes them bigger, and
// finishFrames tidies them into the mod - their size checked against the
// original's, their outline the original's own (the upscaler's blurry edge
// replaced by the original's alpha, scaled smoothly).
namespace HdArt {

// The roots read again (Mods' list changed, a mod switched on or off)
void refresh();
// Whether any HD art is at work
bool active();
// ours: how the HD frames are kept on the graphics card - the first HD art
// mod's setting (Mods::kHdCompression): 0 full size, 1 compressed (fast),
// 2 compressed (best quality)
int compression();
// Counts each refresh (frames made from the old roots are made again)
int generation();
// The frame's HD art (path: "<folder>/<direction>_<frame>"), or nullptr
SDL_Surface *load(const std::string &path);
// ours: several frames' HD art at once, read and decoded on all cores (each
// as load; the caller owns them). One at a time, a big animal coming into a
// zoomed-in view stopped the game for seconds while its frames decoded.
// level 2: each halved from the pack's 4x (a quarter of the memory, and
// steadier than 4x drawn small, without mipmaps, when zoomed in only a
// little); 4: as packed
std::vector<SDL_Surface *> loadMany(const std::vector<std::string> &paths, int level = 4);
// ours: the background loader. A direction's frames (paths), wanted at a
// level, decoded on worker threads while the game goes on; the owner (any
// number, unique to it) takes them when they're ready. What was wanted most
// recently goes first; a request not made again for a couple of seconds is
// dropped, and frames made but not taken are freed after a while. Called
// from the main thread; only the decoding runs elsewhere.
// encode: each frame also encoded for the graphics card there (1 fast, 2
// best: GpuCompressed::encode), the blocks carried with it (encodedOf) -
// only for frames used as they are (not recoloured or relit). A frame
// taken is freed with freeFrame (its blocks with it)
void requestFrames(uint64_t owner, const std::string &key, int level, const std::vector<std::string> &paths,
                   int encode = 0);
const GpuCompressed::Blocks *encodedOf(SDL_Surface *frame);
void freeFrame(SDL_Surface *frame);
bool takeFrames(uint64_t owner, const std::string &key, int level, std::vector<SDL_Surface *> &out);
bool framesReady(uint64_t owner, const std::string &key, int level);
// (its requests and frames dropped: it's gone, or its frames are)
void cancelFrames(uint64_t owner);
// (once a frame: what's stale dropped)
void loaderTick();
// A frame halved, each 2 x 2 averaged by its alpha (a new RGBA32 surface)
SDL_Surface *halve(SDL_Surface *s);
// What load has found and missed so far, and the roots (the console's hdart)
std::string stats();

// Where recolour finds art in its own palette (main sets it)
void setResources(ResourceManager *rm);
// HD art for recoloured art (Animation::setRecoloured: a building's picked
// colours, a keeper's hair and skin, a guest's clothes). The pack's frame is
// made from the art in its own palette; here each HD pixel gets the colour
// change of the original pixel nearby whose own colour it's nearest (so the
// upscaler's sharp edges between parts stay sharp). painted: the frame as
// drawn; name, direction, frame: the art, to find it in its own palette.
// Returns the recoloured frame (hd freed), or hd as it was
SDL_Surface *recolour(SDL_Surface *hd, SDL_Surface *painted, const std::string &name, const std::string &direction,
                      int frame);

// Every frame of the map art whose .ani paths start with one of prefixes
// (none: every one that isn't ui/), written under outDir. Returns how many
int exportFrames(ResourceManager *rm, const std::string &outDir, const std::vector<std::string> &prefixes);
// What exportFrames would write - animations, frames and pixels per top
// folder - logged, nothing written (--hd-survey)
void survey(ResourceManager *rm, const std::vector<std::string> &prefixes);
// Each upscaled frame under upDir matched with its original under
// framesDir, tidied and written under outDir. Returns how many
int finishFrames(const std::string &framesDir, const std::string &upDir, const std::string &outDir);

} // namespace HdArt

#endif // HD_ART_HPP
