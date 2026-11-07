#ifndef BC7_HPP
#define BC7_HPP

#include <cstddef>
#include <cstdint>

// ours: BC7, the GPU's own compressed texture format (a 4 x 4 block of
// pixels in 16 bytes - a quarter of RGBA's memory - read by the GPU as it
// draws, no unpacking). Written here in modes 1, 3, 5, 6 and 7 - each
// block in whichever of them decodes closest (Bc7.cpp) - and read back in
// those modes (the checks).
// The format: Microsoft, "BC7 Format", learn.microsoft.com/windows/win32/
// direct3d11/bc7-format, and the OpenGL ARB_texture_compression_bptc spec.
namespace Bc7 {

// rgba: w x h pixels, 4 bytes each in R, G, B, A order, pitch bytes a row;
// w and h multiples of 4. out: (w / 4) * (h / 4) blocks of 16 bytes, row by
// row. Split across the CPU's cores (threads 0: all of them). best: every
// mode written here tried on each block (slower); else mode 6 only, which
// keeps smooth colour and soft edges close (busy colour less so)
void encode(const uint8_t *rgba, int w, int h, int pitch, uint8_t *out, int threads = 0, bool best = true);

// One block back to its 16 pixels (R, G, B, A each, row by row) - the checks
// (a block of a mode not written here - 0, 2, 4 - false)
bool decodeBlock(const uint8_t block[16], uint8_t rgba[64]);

} // namespace Bc7

#endif // BC7_HPP
