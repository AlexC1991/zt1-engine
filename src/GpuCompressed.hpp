#ifndef GPU_COMPRESSED_HPP
#define GPU_COMPRESSED_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

// ours: an HD art mod's zoomed-in frames held on the GPU compressed (BC7:
// Bc7.hpp), a quarter of the memory of the ordinary RGBA texture, drawn as
// any other texture (SDL draws it; only its storage is changed). SDL 2 can't
// make compressed textures itself, so the texture is made by SDL and its
// storage swapped: on OpenGL through SDL_GL_BindTexture, on Direct3D 11
// through SDL's own texture record (its layout checked first; any doubt and
// nothing is changed). Where it can't be done, nullptr: the ordinary
// texture's made instead. The HD art mod's setting picks it (Mods::kHdCompression).
namespace GpuCompressed {

// Whether this renderer can take them (asked of it once)
bool supported(SDL_Renderer *renderer);

// A frame encoded (encode, on any thread - the HD loader's) and made a
// texture later (upload, on the renderer's thread): w x h, made up to
// w4 x h4, its blocks
struct Blocks {
  int w = 0, h = 0, w4 = 0, h4 = 0;
  bool best = false;
  std::vector<uint8_t> data;
};
bool encode(SDL_Surface *surface, bool best, Blocks &out, int threads = 0);
SDL_Texture *upload(SDL_Renderer *renderer, const Blocks &blocks, int scale);

// surface (any format) as a BC7 texture standing for art scale times
// smaller, marked as ArtScaler does (querySize, contentRect): its size made
// up to whole blocks of 4 with its edge pixels repeated; best: every BC7
// mode tried (Bc7::encode). nullptr: not here
SDL_Texture *create(SDL_Renderer *renderer, SDL_Surface *surface, int scale, bool best = false);

// How many have been made and their bytes on the GPU (the logs)
std::string stats();

// The check (the panel walk-through): a made-up picture - smooth colour,
// noise, soft and hard edges, a size that isn't whole blocks - encoded and
// decoded close to itself, and, where this renderer takes them, drawn from
// the GPU exactly as decoded. "" when it passes, else what went wrong
std::string selfCheck(SDL_Renderer *renderer);

} // namespace GpuCompressed

#endif // GPU_COMPRESSED_HPP
