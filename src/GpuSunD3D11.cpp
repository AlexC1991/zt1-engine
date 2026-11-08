#include "GpuSunD3D11.hpp"

// ours: see GpuSunD3D11.hpp

#include "D3D11Util.hpp"

#ifdef _WIN32
#include <algorithm>
#include <cstring>

namespace GpuSunD3D11 {

// SunDepth::shadow, line for line: the 2 x 2 texels about the point (its own
// shape's skipped), the slope-scaled tolerance, how much each caster stops,
// and the soft edge's five taps
const char *kHlsl = R"(
Texture2D<float> sunDepth0 : register(t5);
Texture2D<uint> sunOwner0 : register(t6);
Texture2D<float> sunDepth1 : register(t7);
Texture2D<uint> sunOwner1 : register(t8);
StructuredBuffer<float> sunOpacity : register(t9);
cbuffer SunMaps : register(b1) {
  float4 sunMapA[2]; // x0, y0, pixel scale, size
  float4 sunMapB[2]; // dx, dy, span, soften
  uint sunOpacityCount; uint3 sunPad;
};
float sunTap(Texture2D<float> depthMap, Texture2D<uint> ownerMap, int size, float sx, float sy, float pz, float bias, float inv, uint receiver) {
  const int tx = (int)floor(sx), ty = (int)floor(sy);
  const float gx = sx - tx, gy = sy - ty;
  float amount = 0.0;
  [unroll] for (int dy = 0; dy < 2; ++dy) [unroll] for (int dx = 0; dx < 2; ++dx) {
    const int xx = tx + dx, yy = ty + dy;
    if (xx < 0 || yy < 0 || xx >= size || yy >= size) continue;
    const uint by = ownerMap.Load(int3(xx, yy, 0));
    if (by == receiver) continue;
    const float weight = (dx != 0 ? gx : 1.0 - gx) * (dy != 0 ? gy : 1.0 - gy);
    const float gap = depthMap.Load(int3(xx, yy, 0)) - pz - bias;
    const float stops = by < sunOpacityCount ? sunOpacity[by] : 1.0;
    amount += weight * clamp(gap * inv, 0.0, 1.0) * stops;
  }
  return amount;
}
float sunShadowOne(Texture2D<float> depthMap, Texture2D<uint> ownerMap, float4 a, float4 b, float3 p, float3 n, uint receiver) {
  const int size = (int)a.w;
  if (size == 0) return 0.0;
  const float x = (p.x + b.x * p.z - a.x) * a.z - 0.5, y = (p.y + b.y * p.z - a.y) * a.z - 0.5;
  const float bias = 0.10 + (b.z / (float)size) * (0.35 + sqrt(n.x * n.x + n.y * n.y)) * 2.0;
  const float inv = 1.0 / max(0.08, bias * 0.5);
  if (b.w <= 0.0) return sunTap(depthMap, ownerMap, size, x, y, p.z, bias, inv, receiver);
  const float s = b.w;
  return sunTap(depthMap, ownerMap, size, x, y, p.z, bias, inv, receiver) * 0.4 +
         (sunTap(depthMap, ownerMap, size, x - s, y - s, p.z, bias, inv, receiver) +
          sunTap(depthMap, ownerMap, size, x + s, y - s, p.z, bias, inv, receiver) +
          sunTap(depthMap, ownerMap, size, x - s, y + s, p.z, bias, inv, receiver) +
          sunTap(depthMap, ownerMap, size, x + s, y + s, p.z, bias, inv, receiver)) * 0.15;
}
float sunShadowPair(float3 p, float3 n, uint receiver, float blend) {
  return sunShadowOne(sunDepth0, sunOwner0, sunMapA[0], sunMapB[0], p, n, receiver) * (1.0 - blend) +
         sunShadowOne(sunDepth1, sunOwner1, sunMapA[1], sunMapB[1], p, n, receiver) * blend;
}
)";

namespace {

template <typename T> void release(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

constexpr int kTile = 32; // (the texels a moving actor's update is rounded out to)

struct Slot {
  ID3D11Texture2D *depth = nullptr, *owner = nullptr;
  ID3D11ShaderResourceView *depthView = nullptr, *ownerView = nullptr;
  int size = 0;
  uint64_t stamp = 0;
  std::vector<SDL_Rect> last; // (the actors' regions as last sent)
  void drop() {
    release(depthView);
    release(ownerView);
    release(depth);
    release(owner);
    size = 0;
    stamp = 0;
    last.clear();
  }
  bool make(int s) {
    drop();
    auto *dev = D3D11Util::device();
    D3D11_TEXTURE2D_DESC d{};
    d.Width = d.Height = UINT(s);
    d.MipLevels = d.ArraySize = 1;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.Format = DXGI_FORMAT_R32_FLOAT;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &depth)))
      return drop(), false;
    d.Format = DXGI_FORMAT_R32_UINT;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &owner)))
      return drop(), false;
    if (FAILED(dev->CreateShaderResourceView(depth, nullptr, &depthView)) ||
        FAILED(dev->CreateShaderResourceView(owner, nullptr, &ownerView)))
      return drop(), false;
    size = s;
    return true;
  }
};

struct Constants {
  float a[2][4];
  float b[2][4];
  uint32_t opacityCount, pad[3];
};

Slot g_slot[2];
int g_bound[2] = {0, 1}; // (the slot each map, 0 and 1, is in)
ID3D11Buffer *g_cb = nullptr;
ID3D11Buffer *g_opacity = nullptr;
ID3D11ShaderResourceView *g_opacityView = nullptr;
UINT g_opacityCapacity = 0;
Constants g_constants{};
std::vector<uint8_t> g_dirty;
uint64_t g_frame = ~0ull;
bool g_frameSent = false;

void sendRows(Slot &slot, const LightingAssets::SunDepth &m, int x, int y, int w, int h) {
  auto *ctx = D3D11Util::context();
  const D3D11_BOX box{UINT(x), UINT(y), 0, UINT(x + w), UINT(y + h), 1};
  const size_t at = size_t(y) * m.size + x;
  ctx->UpdateSubresource(slot.depth, 0, &box, m.depth.data() + at, UINT(m.size * sizeof(float)), 0);
  ctx->UpdateSubresource(slot.owner, 0, &box, m.owner.data() + at, UINT(m.size * sizeof(uint32_t)), 0);
}

bool sendOpacity(const std::vector<float> *opacity) {
  const UINT count = opacity ? UINT(opacity->size()) : 0;
  g_constants.opacityCount = count;
  const UINT need = std::max<UINT>(count, 1);
  if (!g_opacity || g_opacityCapacity < need) {
    release(g_opacityView);
    release(g_opacity);
    g_opacityCapacity = std::max(need, g_opacityCapacity * 2);
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = g_opacityCapacity * sizeof(float);
    d.Usage = D3D11_USAGE_DYNAMIC;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    d.StructureByteStride = sizeof(float);
    if (FAILED(D3D11Util::device()->CreateBuffer(&d, nullptr, &g_opacity)))
      return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC v{};
    v.Format = DXGI_FORMAT_UNKNOWN;
    v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    v.Buffer.NumElements = g_opacityCapacity;
    if (FAILED(D3D11Util::device()->CreateShaderResourceView(g_opacity, &v, &g_opacityView)))
      return release(g_opacity), false;
  }
  if (!count)
    return true;
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(D3D11Util::context()->Map(g_opacity, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    return false;
  std::memcpy(mapped.pData, opacity->data(), count * sizeof(float));
  D3D11Util::context()->Unmap(g_opacity, 0);
  return true;
}

} // namespace

bool upload(const LightingAssets::SunDepth (&maps)[2], const std::vector<float> *opacity, uint64_t frame) {
  if (!D3D11Util::context())
    return false;
  if (frame == g_frame)
    return g_frameSent;
  // (a frame went by unsent: what the card holds no longer matches the maps)
  if (frame != g_frame + 1)
    for (auto &s : g_slot)
      s.stamp = 0;
  g_frame = frame;
  g_frameSent = false;
  if (!g_cb) {
    D3D11_BUFFER_DESC b{};
    b.ByteWidth = sizeof(Constants);
    b.Usage = D3D11_USAGE_DEFAULT;
    b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(D3D11Util::device()->CreateBuffer(&b, nullptr, &g_cb)))
      return false;
  }
  // (each map in the slot already holding it, when one does: the two swap
  // places as the day goes on - DayNightLighting::updateSun)
  int at[2] = {-1, -1};
  for (int k = 0; k < 2; ++k)
    for (int s = 0; s < 2; ++s)
      if (maps[k].stamp && g_slot[s].stamp == maps[k].stamp && g_slot[s].size == maps[k].size && at[1 - k] != s)
        at[k] = s;
  for (int k = 0; k < 2; ++k)
    if (at[k] < 0)
      at[k] = at[1 - k] >= 0 ? 1 - at[1 - k] : k;
  for (int k = 0; k < 2; ++k) {
    const auto &m = maps[k];
    auto &slot = g_slot[at[k]];
    g_bound[k] = at[k];
    auto &a = g_constants.a[k];
    auto &b = g_constants.b[k];
    const bool usable = m.size > 0 && m.depth.size() == size_t(m.size) * m.size && m.owner.size() == m.depth.size();
    a[0] = m.x0;
    a[1] = m.y0;
    a[2] = m.pixelScale;
    a[3] = usable ? float(m.size) : 0.0f;
    b[0] = m.dx;
    b[1] = m.dy;
    b[2] = m.span;
    b[3] = m.soften;
    if (!usable) {
      if (!slot.depth && !slot.make(1))
        return false;
      slot.stamp = 0;
      continue;
    }
    if (slot.size != m.size && !slot.make(m.size))
      return false;
    if (slot.stamp != m.stamp) {
      sendRows(slot, m, 0, 0, m.size, m.size);
      slot.stamp = m.stamp;
    } else {
      // (the tiles under the actors' regions as last sent - put back as the
      // map has them now - and under this frame's, rows of tiles at a time)
      const int tiles = (m.size + kTile - 1) / kTile;
      g_dirty.assign(size_t(tiles) * tiles, 0);
      for (const std::vector<SDL_Rect> *list : {static_cast<const std::vector<SDL_Rect> *>(&slot.last), &m.actorRegions})
        for (const auto &r : *list) {
          const int x0 = std::max(0, r.x / kTile), y0 = std::max(0, r.y / kTile);
          const int x1 = std::min(tiles - 1, (r.x + r.w - 1) / kTile), y1 = std::min(tiles - 1, (r.y + r.h - 1) / kTile);
          for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
              g_dirty[size_t(y) * tiles + x] = 1;
        }
      for (int y = 0; y < tiles; ++y)
        for (int x = 0; x < tiles;) {
          if (!g_dirty[size_t(y) * tiles + x]) {
            ++x;
            continue;
          }
          int end = x;
          while (end < tiles && g_dirty[size_t(y) * tiles + end])
            ++end;
          const int px = x * kTile, py = y * kTile;
          sendRows(slot, m, px, py, std::min(m.size, end * kTile) - px, std::min(m.size, py + kTile) - py);
          x = end;
        }
    }
    slot.last = m.actorRegions;
  }
  if (!sendOpacity(opacity))
    return false;
  D3D11Util::context()->UpdateSubresource(g_cb, 0, nullptr, &g_constants, 0, 0);
  return g_frameSent = true;
}

void bindGeometry() {
  auto *ctx = D3D11Util::context();
  ID3D11ShaderResourceView *views[5] = {g_slot[g_bound[0]].depthView, g_slot[g_bound[0]].ownerView,
                                        g_slot[g_bound[1]].depthView, g_slot[g_bound[1]].ownerView, g_opacityView};
  ctx->GSSetShaderResources(5, 5, views);
  ctx->GSSetConstantBuffers(1, 1, &g_cb);
}

void unbindGeometry() {
  auto *ctx = D3D11Util::context();
  ID3D11ShaderResourceView *none[5] = {};
  ctx->GSSetShaderResources(5, 5, none);
  ID3D11Buffer *noBuffer = nullptr;
  ctx->GSSetConstantBuffers(1, 1, &noBuffer);
}

} // namespace GpuSunD3D11

#else

namespace GpuSunD3D11 {
const char *kHlsl = "";
bool upload(const LightingAssets::SunDepth (&)[2], const std::vector<float> *, uint64_t) { return false; }
void bindGeometry() {}
void unbindGeometry() {}
} // namespace GpuSunD3D11
#endif
