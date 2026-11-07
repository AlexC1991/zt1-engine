#include "Bc7.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

// ours: a BC7 encoder (see Bc7.hpp). Each block is tried in the modes that
// suit it - solid blocks in mode 6 (one line of colours), 1 and 3 (two
// groups of pixels, each on its own line: busy colour such as leaves), soft
// or clear ones in 6, 5 (colour and alpha on separate steps) and 7 (two
// groups with alpha) - and the one that decodes closest is kept. A group's
// line: the mean and strongest direction of its colours' spread, its ends
// where they reach along it, rounded to the mode's bits, every pixel given
// its nearest step, then the ends fitted again to those steps by least
// squares while that comes closer. The two-group modes' pattern (one of 64)
// is the one whose groups lie closest to a line each, before rounding.
namespace {

constexpr int kW2[4] = {0, 21, 43, 64};
constexpr int kW3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr int kW4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
const int *weightsFor(int ibits) { return ibits == 2 ? kW2 : ibits == 3 ? kW3 : kW4; }

// The format's 64 two-group patterns (each pixel's group, row by row) and
// the second group's anchor pixel - Microsoft, "BC7 Format", and the
// ARB_texture_compression_bptc spec's tables. Checked against the GPU's own
// decoding (GpuCompressed::selfCheck)
const char *const kPartRows[64] = {
    "0011001100110011", "0001000100010001", "0111011101110111", "0001001100110111", "0000000100010011",
    "0011011101111111", "0001001101111111", "0000000100110111", "0000000000010011", "0011011111111111",
    "0000000101111111", "0000000000010111", "0001011111111111", "0000000011111111", "0000111111111111",
    "0000000000001111", "0000100011101111", "0111000100000000", "0000000010001110", "0111001100010000",
    "0011000100000000", "0000100011001110", "0000000010001100", "0111001100110001", "0011000100010000",
    "0000100010001100", "0110011001100110", "0011011001101100", "0001011111101000", "0000111111110000",
    "0111000110001110", "0011100110011100", "0101010101010101", "0000111100001111", "0101101001011010",
    "0011001111001100", "0011110000111100", "0101010110101010", "0110100101101001", "0101101010100101",
    "0111001111001110", "0001001111001000", "0011001001001100", "0011101111011100", "0110100110010110",
    "0011110011000011", "0110011010011001", "0000011001100000", "0100111001000000", "0010011100100000",
    "0000001001110010", "0000010011100100", "0110110010010011", "0011011011001001", "0110001110011100",
    "0011100111000110", "0110110011001001", "0110001100111001", "0111111010000001", "0001100011100111",
    "0000111100110011", "0011001111110000", "0010001011101110", "0100010001110111"};
constexpr int kAnchor2[64] = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                              15, 2,  8,  2,  2,  8,  8,  15, 2,  8,  2,  2,  8,  8,  2,  2,
                              15, 15, 6,  8,  2,  8,  15, 15, 2,  8,  2,  2,  2,  15, 15, 6,
                              6,  2,  6,  8,  15, 15, 2,  2,  15, 15, 15, 15, 15, 2,  2,  15};
struct Patterns {
  uint16_t mask[64]; // (bit i: pixel i is in the second group)
  Patterns() {
    for (int p = 0; p < 64; p++) {
      mask[p] = 0;
      for (int i = 0; i < 16; i++)
        if (kPartRows[p][i] == '1')
          mask[p] |= uint16_t(1u << i);
    }
  }
};
const Patterns &patterns() {
  static const Patterns table;
  return table;
}

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// An end's stored bits (and its low bit, -1: none) as the 8-bit value drawn
inline int expand(int stored, int bits, int pbit) {
  const int v = pbit < 0 ? stored : (stored << 1) | pbit;
  const int n = bits + (pbit < 0 ? 0 : 1);
  int x = v << (8 - n);
  x |= x >> n;
  return x & 255;
}

// How one group is stored: bits a channel, its low bits (0 none, 1 one for
// both ends, 2 one each), bits a step, and its channels (c0, nc of them)
struct Fit {
  int bits, pmode, ibits, c0, nc;
};

struct Group {
  int s[2][4] = {};
  int p[2] = {0, 0};
};

int nearestStored(float e, int bits, int pbit, int &err) {
  const int top = (1 << bits) - 1;
  const int guess = clampi(static_cast<int>(e * top / 255.0f + 0.5f), 0, top);
  int best = guess, bestErr = 1 << 30;
  for (int s = std::max(0, guess - 2); s <= std::min(top, guess + 2); s++) {
    const int d = expand(s, bits, pbit) - static_cast<int>(e + 0.5f);
    if (d * d < bestErr) {
      bestErr = d * d;
      best = s;
    }
  }
  err = bestErr;
  return best;
}

void quantize(const Fit &f, const float ends[2][4], Group &g) {
  auto one = [&](int end, int pbit, int out[4]) {
    int total = 0;
    for (int c = f.c0; c < f.c0 + f.nc; c++) {
      int e = 0;
      out[c] = nearestStored(ends[end][c], f.bits, pbit, e);
      total += e;
    }
    return total;
  };
  if (f.pmode == 0) {
    one(0, -1, g.s[0]);
    one(1, -1, g.s[1]);
  } else if (f.pmode == 2) {
    for (int end = 0; end < 2; end++) {
      int a[4] = {}, b[4] = {};
      const int e0 = one(end, 0, a), e1 = one(end, 1, b);
      g.p[end] = e1 < e0 ? 1 : 0;
      std::memcpy(g.s[end], e1 < e0 ? b : a, sizeof a);
    }
  } else {
    int a[2][4] = {}, b[2][4] = {};
    const int e0 = one(0, 0, a[0]) + one(1, 0, a[1]), e1 = one(0, 1, b[0]) + one(1, 1, b[1]);
    g.p[0] = g.p[1] = e1 < e0 ? 1 : 0;
    std::memcpy(g.s, e1 < e0 ? b : a, sizeof a);
  }
}

int endValue(const Fit &f, const Group &g, int end, int c) {
  return expand(g.s[end][c], f.bits, f.pmode ? g.p[end] : -1);
}

// Each of the group's pixels given its nearest step; the summed error
int assign(const uint8_t px[16][4], const int *pix, int n, const Fit &f, const Group &g, uint8_t idx[16]) {
  const int *w = weightsFor(f.ibits), steps = 1 << f.ibits;
  int pal[16][4];
  for (int k = 0; k < steps; k++)
    for (int c = f.c0; c < f.c0 + f.nc; c++)
      pal[k][c] = ((64 - w[k]) * endValue(f, g, 0, c) + w[k] * endValue(f, g, 1, c) + 32) >> 6;
  int total = 0;
  for (int j = 0; j < n; j++) {
    const int i = pix[j];
    int bestErr = 1 << 30, bestK = 0;
    for (int k = 0; k < steps; k++) {
      int err = 0;
      for (int c = f.c0; c < f.c0 + f.nc; c++) {
        const int d = pal[k][c] - px[i][c];
        err += d * d;
      }
      if (err < bestErr) {
        bestErr = err;
        bestK = k;
      }
    }
    idx[i] = static_cast<uint8_t>(bestK);
    total += bestErr;
  }
  return total;
}

// The group's colours' line: its ends
void lineEnds(const uint8_t px[16][4], const int *pix, int n, const Fit &f, float ends[2][4]) {
  float m[4] = {};
  for (int j = 0; j < n; j++)
    for (int c = f.c0; c < f.c0 + f.nc; c++)
      m[c] += px[pix[j]][c];
  for (int c = f.c0; c < f.c0 + f.nc; c++)
    m[c] /= static_cast<float>(n);
  float cov[4][4] = {};
  for (int j = 0; j < n; j++)
    for (int r = f.c0; r < f.c0 + f.nc; r++)
      for (int c = f.c0; c < f.c0 + f.nc; c++)
        cov[r][c] += (px[pix[j]][r] - m[r]) * (px[pix[j]][c] - m[c]);
  int widest = f.c0;
  for (int c = f.c0; c < f.c0 + f.nc; c++)
    if (cov[c][c] > cov[widest][widest])
      widest = c;
  float axis[4] = {};
  for (int c = f.c0; c < f.c0 + f.nc; c++)
    axis[c] = cov[widest][c];
  bool flat = cov[widest][widest] < 1e-3f;
  for (int it = 0; it < 8 && !flat; it++) {
    float next[4] = {}, len = 0;
    for (int r = f.c0; r < f.c0 + f.nc; r++) {
      for (int c = f.c0; c < f.c0 + f.nc; c++)
        next[r] += cov[r][c] * axis[c];
      len += next[r] * next[r];
    }
    len = std::sqrt(len);
    if (len < 1e-6f) {
      flat = true;
      break;
    }
    for (int c = f.c0; c < f.c0 + f.nc; c++)
      axis[c] = next[c] / len;
  }
  float tmin = 0, tmax = 0;
  if (!flat) {
    tmin = 1e30f;
    tmax = -1e30f;
    for (int j = 0; j < n; j++) {
      float t = 0;
      for (int c = f.c0; c < f.c0 + f.nc; c++)
        t += (px[pix[j]][c] - m[c]) * axis[c];
      tmin = std::min(tmin, t);
      tmax = std::max(tmax, t);
    }
  }
  for (int c = f.c0; c < f.c0 + f.nc; c++) {
    ends[0][c] = std::clamp(m[c] + tmin * axis[c], 0.0f, 255.0f);
    ends[1][c] = std::clamp(m[c] + tmax * axis[c], 0.0f, 255.0f);
  }
}

// The ends that best fit the pixels at their steps (least squares)
bool refit(const uint8_t px[16][4], const int *pix, int n, const Fit &f, const uint8_t idx[16], float out[2][4]) {
  const int *w = weightsFor(f.ibits);
  float a = 0, b = 0, c = 0, r0[4] = {}, r1[4] = {};
  for (int j = 0; j < n; j++) {
    const int i = pix[j];
    const float t = w[idx[i]] / 64.0f, v = 1.0f - t;
    a += v * v;
    b += v * t;
    c += t * t;
    for (int ch = f.c0; ch < f.c0 + f.nc; ch++) {
      r0[ch] += v * px[i][ch];
      r1[ch] += t * px[i][ch];
    }
  }
  const float det = a * c - b * b;
  if (std::fabs(det) < 1e-4f)
    return false;
  for (int ch = f.c0; ch < f.c0 + f.nc; ch++) {
    out[0][ch] = std::clamp((c * r0[ch] - b * r1[ch]) / det, 0.0f, 255.0f);
    out[1][ch] = std::clamp((a * r1[ch] - b * r0[ch]) / det, 0.0f, 255.0f);
  }
  return true;
}

// One group fitted: its ends and its pixels' steps
void fitGroup(const uint8_t px[16][4], const int *pix, int n, const Fit &f, Group &g, uint8_t idx[16]) {
  float ends[2][4] = {};
  lineEnds(px, pix, n, f, ends);
  quantize(f, ends, g);
  int err = assign(px, pix, n, f, g, idx);
  for (int round = 0; round < 2 && err > 0; round++) {
    float again[2][4] = {};
    if (!refit(px, pix, n, f, idx, again))
      break;
    Group g2;
    uint8_t idx2[16];
    std::memcpy(idx2, idx, 16);
    quantize(f, again, g2);
    const int err2 = assign(px, pix, n, f, g2, idx2);
    if (err2 >= err)
      break;
    err = err2;
    g = g2;
    std::memcpy(idx, idx2, 16);
  }
}

// (the anchor pixel's step is stored a bit short, its top bit 0: the ends
// swapped and the group's steps turned round when it's in the top half)
void anchor(const Fit &f, Group &g, const int *pix, int n, int at, uint8_t idx[16]) {
  const int top = (1 << f.ibits) - 1;
  if (idx[at] <= top / 2)
    return;
  for (int c = 0; c < 4; c++)
    std::swap(g.s[0][c], g.s[1][c]);
  std::swap(g.p[0], g.p[1]);
  for (int j = 0; j < n; j++)
    idx[pix[j]] = static_cast<uint8_t>(top - idx[pix[j]]);
}

struct Bits {
  uint8_t *out;
  int pos = 0;
  void put(uint32_t v, int n) {
    for (int i = 0; i < n; i++, pos++)
      if ((v >> i) & 1u)
        out[pos >> 3] |= static_cast<uint8_t>(1u << (pos & 7));
  }
};

const int kAll[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

// Mode 6: one group, RGBA, 7 bits and a low bit each end, 4-bit steps
void encodeMode6(const uint8_t px[16][4], uint8_t out[16]) {
  const Fit f{7, 2, 4, 0, 4};
  Group g;
  uint8_t idx[16] = {};
  fitGroup(px, kAll, 16, f, g, idx);
  anchor(f, g, kAll, 16, 0, idx);
  std::memset(out, 0, 16);
  Bits b{out};
  b.put(1u << 6, 7);
  for (int c = 0; c < 4; c++) {
    b.put(uint32_t(g.s[0][c]), 7);
    b.put(uint32_t(g.s[1][c]), 7);
  }
  b.put(uint32_t(g.p[0]), 1);
  b.put(uint32_t(g.p[1]), 1);
  for (int i = 0; i < 16; i++)
    b.put(idx[i], i == 0 ? 3 : 4);
}

// Mode 5: one group, colour (7 bits) and alpha (8 bits) each on its own
// 2-bit steps
void encodeMode5(const uint8_t px[16][4], uint8_t out[16]) {
  const Fit fc{7, 0, 2, 0, 3}, fa{8, 0, 2, 3, 1};
  Group gc, ga;
  uint8_t ic[16] = {}, ia[16] = {};
  fitGroup(px, kAll, 16, fc, gc, ic);
  fitGroup(px, kAll, 16, fa, ga, ia);
  anchor(fc, gc, kAll, 16, 0, ic);
  anchor(fa, ga, kAll, 16, 0, ia);
  std::memset(out, 0, 16);
  Bits b{out};
  b.put(1u << 5, 6);
  b.put(0, 2); // (no rotation)
  for (int c = 0; c < 3; c++) {
    b.put(uint32_t(gc.s[0][c]), 7);
    b.put(uint32_t(gc.s[1][c]), 7);
  }
  b.put(uint32_t(ga.s[0][3]), 8);
  b.put(uint32_t(ga.s[1][3]), 8);
  for (int i = 0; i < 16; i++)
    b.put(ic[i], i == 0 ? 1 : 2);
  for (int i = 0; i < 16; i++)
    b.put(ia[i], i == 0 ? 1 : 2);
}

// Modes 1, 3 and 7: two groups by pattern part
void encodeTwoGroups(const uint8_t px[16][4], int mode, int part, uint8_t out[16]) {
  const Fit f = mode == 1 ? Fit{6, 1, 3, 0, 3} : mode == 3 ? Fit{7, 2, 2, 0, 3} : Fit{5, 2, 2, 0, 4};
  const uint16_t mask = patterns().mask[part];
  int pix[2][16], n[2] = {0, 0};
  for (int i = 0; i < 16; i++) {
    const int s = (mask >> i) & 1;
    pix[s][n[s]++] = i;
  }
  Group g[2];
  uint8_t idx[16] = {};
  for (int s = 0; s < 2; s++) {
    fitGroup(px, pix[s], n[s], f, g[s], idx);
    anchor(f, g[s], pix[s], n[s], s == 0 ? 0 : kAnchor2[part], idx);
  }
  std::memset(out, 0, 16);
  Bits b{out};
  b.put(1u << mode, mode + 1);
  b.put(uint32_t(part), 6);
  for (int c = 0; c < f.nc; c++)
    for (int s = 0; s < 2; s++)
      for (int e = 0; e < 2; e++)
        b.put(uint32_t(g[s].s[e][c]), f.bits);
  if (f.pmode == 1) {
    b.put(uint32_t(g[0].p[0]), 1);
    b.put(uint32_t(g[1].p[0]), 1);
  } else {
    for (int s = 0; s < 2; s++)
      for (int e = 0; e < 2; e++)
        b.put(uint32_t(g[s].p[e]), 1);
  }
  for (int i = 0; i < 16; i++)
    b.put(idx[i], f.ibits - ((i == 0 || i == kAnchor2[part]) ? 1 : 0));
}

// The patterns whose two groups lie closest to a line each (count of them,
// best first), over nc channels: what's left of each group's spread off
// its strongest direction
void bestPatterns(const uint8_t px[16][4], int nc, int count, int *out) {
  // (each pixel's sums, so a group's covariance is a masked sum)
  float sums[16][14] = {};
  for (int i = 0; i < 16; i++) {
    int k = 0;
    for (int c = 0; c < nc; c++)
      sums[i][k++] = px[i][c];
    for (int r = 0; r < nc; r++)
      for (int c = r; c < nc; c++)
        sums[i][k++] = float(px[i][r]) * px[i][c];
  }
  float cost[64];
  for (int part = 0; part < 64; part++) {
    const uint16_t mask = patterns().mask[part];
    float total = 0;
    for (int s = 0; s < 2; s++) {
      float acc[14] = {};
      int n = 0;
      for (int i = 0; i < 16; i++)
        if (int((mask >> i) & 1) == s) {
          n++;
          for (int k = 0; k < 14; k++)
            acc[k] += sums[i][k];
        }
      if (n < 2)
        continue;
      float m[4] = {}, cov[4][4] = {};
      for (int c = 0; c < nc; c++)
        m[c] = acc[c] / n;
      int k = nc;
      for (int r = 0; r < nc; r++)
        for (int c = r; c < nc; c++, k++)
          cov[r][c] = cov[c][r] = acc[k] - n * m[r] * m[c];
      float trace = 0, axis[4] = {1, 1, 1, 1};
      for (int c = 0; c < nc; c++)
        trace += cov[c][c];
      float lambda = 0;
      for (int it = 0; it < 4; it++) {
        float next[4] = {}, len = 0;
        for (int r = 0; r < nc; r++) {
          for (int c = 0; c < nc; c++)
            next[r] += cov[r][c] * axis[c];
          len += next[r] * next[r];
        }
        len = std::sqrt(len);
        if (len < 1e-6f)
          break;
        float l2 = 0;
        for (int c = 0; c < nc; c++) {
          axis[c] = next[c] / len;
          l2 += axis[c] * axis[c];
        }
        lambda = len / std::sqrt(std::max(l2, 1e-12f));
      }
      total += std::max(0.0f, trace - lambda);
    }
    cost[part] = total;
  }
  for (int k = 0; k < count; k++) {
    int best = -1;
    for (int part = 0; part < 64; part++) {
      bool taken = false;
      for (int j = 0; j < k; j++)
        taken = taken || out[j] == part;
      if (!taken && (best < 0 || cost[part] < cost[best]))
        best = part;
    }
    out[k] = best;
  }
}

int blockError(const uint8_t px[16][4], const uint8_t block[16]) {
  uint8_t got[64];
  if (!Bc7::decodeBlock(block, got))
    return 1 << 30;
  int err = 0;
  for (int i = 0; i < 16; i++)
    for (int c = 0; c < 4; c++) {
      const int d = int(got[i * 4 + c]) - px[i][c];
      err += d * d;
    }
  return err;
}

void encodeBlock(const uint8_t px[16][4], uint8_t out[16], bool best) {
  bool opaque = true, same = true;
  for (int i = 0; i < 16; i++) {
    opaque = opaque && px[i][3] == 255;
    same = same && std::memcmp(px[i], px[0], 4) == 0;
  }
  encodeMode6(px, out);
  if (same || !best)
    return;
  int least = blockError(px, out);
  // (close already - under 2 levels a channel on average: the other modes
  // can't come much closer, and they're most of the time)
  if (least <= 16 * 4 * 4)
    return;
  uint8_t trial[16];
  auto consider = [&]() {
    const int err = blockError(px, trial);
    if (err < least) {
      least = err;
      std::memcpy(out, trial, 16);
    }
  };
  int parts[2];
  if (opaque) {
    bestPatterns(px, 3, 2, parts);
    for (int part : parts) {
      if (least == 0)
        break;
      encodeTwoGroups(px, 1, part, trial);
      consider();
      encodeTwoGroups(px, 3, part, trial);
      consider();
    }
  } else {
    encodeMode5(px, trial);
    consider();
    bestPatterns(px, 4, 2, parts);
    for (int part : parts) {
      if (least == 0)
        break;
      encodeTwoGroups(px, 7, part, trial);
      consider();
    }
  }
}

struct Reader {
  const uint8_t *in;
  int pos = 0;
  int get(int n) {
    int v = 0;
    for (int i = 0; i < n; i++, pos++)
      v |= ((in[pos >> 3] >> (pos & 7)) & 1) << i;
    return v;
  }
};

} // namespace

namespace Bc7 {

void encode(const uint8_t *rgba, int w, int h, int pitch, uint8_t *out, int threads, bool best) {
  const int bw = w / 4, bh = h / 4;
  if (bw <= 0 || bh <= 0)
    return;
  patterns(); // (made before the threads)
  if (threads <= 0)
    threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 16);
  threads = std::clamp(std::min(threads, bh / 4), 1, 16);
  auto rows = [&](int first) {
    uint8_t px[16][4];
    for (int by = first; by < bh; by += threads)
      for (int bx = 0; bx < bw; bx++) {
        for (int y = 0; y < 4; y++)
          std::memcpy(px[y * 4], rgba + size_t(by * 4 + y) * pitch + size_t(bx) * 16, 16);
        encodeBlock(px, out + (size_t(by) * bw + bx) * 16, best);
      }
  };
  if (threads == 1) {
    rows(0);
    return;
  }
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; t++)
    pool.emplace_back(rows, t);
  for (std::thread &th : pool)
    th.join();
}

bool decodeBlock(const uint8_t block[16], uint8_t rgba[64]) {
  Reader r{block};
  int mode = 0;
  while (mode < 8 && !r.get(1))
    mode++;
  if (mode == 5) {
    const int rotation = r.get(2);
    int e[2][4];
    for (int c = 0; c < 3; c++)
      for (int k = 0; k < 2; k++)
        e[k][c] = expand(r.get(7), 7, -1);
    for (int k = 0; k < 2; k++)
      e[k][3] = r.get(8);
    int ic[16], ia[16];
    for (int i = 0; i < 16; i++)
      ic[i] = r.get(i == 0 ? 1 : 2);
    for (int i = 0; i < 16; i++)
      ia[i] = r.get(i == 0 ? 1 : 2);
    for (int i = 0; i < 16; i++) {
      uint8_t *o = rgba + i * 4;
      for (int c = 0; c < 3; c++)
        o[c] = uint8_t(((64 - kW2[ic[i]]) * e[0][c] + kW2[ic[i]] * e[1][c] + 32) >> 6);
      o[3] = uint8_t(((64 - kW2[ia[i]]) * e[0][3] + kW2[ia[i]] * e[1][3] + 32) >> 6);
      if (rotation)
        std::swap(o[3], o[rotation - 1]);
    }
    return true;
  }
  if (mode != 1 && mode != 3 && mode != 6 && mode != 7)
    return false;
  // (groups, pattern bits, colour bits, alpha bits, low bits, step bits)
  const int groups = mode == 6 ? 1 : 2, cbits = mode == 1 ? 6 : mode == 7 ? 5 : 7,
            abits = mode == 6 ? 7 : mode == 7 ? 5 : 0, pmode = mode == 1 ? 1 : 2,
            ibits = mode == 1 ? 3 : mode == 6 ? 4 : 2;
  const int part = groups == 2 ? r.get(6) : 0;
  const int ends = groups * 2;
  int raw[4][4] = {}, pb[4] = {};
  for (int c = 0; c < 3; c++)
    for (int k = 0; k < ends; k++)
      raw[k][c] = r.get(cbits);
  if (abits)
    for (int k = 0; k < ends; k++)
      raw[k][3] = r.get(abits);
  if (pmode == 2)
    for (int k = 0; k < ends; k++)
      pb[k] = r.get(1);
  else
    for (int s = 0; s < groups; s++)
      pb[s * 2] = pb[s * 2 + 1] = r.get(1);
  int e[4][4];
  for (int k = 0; k < ends; k++) {
    for (int c = 0; c < 3; c++)
      e[k][c] = expand(raw[k][c], cbits, pb[k]);
    e[k][3] = abits ? expand(raw[k][3], abits, pb[k]) : 255;
  }
  const int *w = weightsFor(ibits);
  const uint16_t mask = groups == 2 ? patterns().mask[part] : 0;
  for (int i = 0; i < 16; i++) {
    const bool isAnchor = i == 0 || (groups == 2 && i == kAnchor2[part]);
    const int k = r.get(ibits - (isAnchor ? 1 : 0));
    const int s = (mask >> i) & 1;
    for (int c = 0; c < 4; c++)
      rgba[i * 4 + c] = uint8_t(((64 - w[k]) * e[s * 2][c] + w[k] * e[s * 2 + 1][c] + 32) >> 6);
  }
  return true;
}

} // namespace Bc7
