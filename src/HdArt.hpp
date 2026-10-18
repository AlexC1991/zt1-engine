#ifndef HD_ART_HPP
#define HD_ART_HPP

#include <string>
#include <vector>

#include <SDL2/SDL.h>

class ResourceManager;

// HD art: bigger frames standing in for the original's map art (animals,
// people, fences, objects) while the map is zoomed in - at 1x the
// original's own frames are always drawn. Each frame is a PNG, a whole
// multiple of the original's size, at
//
//   <root>/<the .ani's folder>/<direction>_<frame>.png
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
// Counts each refresh (frames made from the old roots are made again)
int generation();
// The frame's HD art (path: "<folder>/<direction>_<frame>"), or nullptr
SDL_Surface *load(const std::string &path);
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
