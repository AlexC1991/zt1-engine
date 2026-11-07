#include "GraphicsEffects.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "GpuFsr.hpp"
#include "Settings.hpp"

// addition: GraphicsEffects.hpp. Each effect a fragment shader (GLSL 1.20,
// as mods' screen shaders: GpuFsr::screenPass) run over the map's picture.
namespace GraphicsEffects {
namespace {

Settings g_settings;
bool g_loaded = false;

// FXAA - Timothy Lottes' fast approximate anti-aliasing (NVIDIA, 2009), in
// its common compact form: along each edge's direction, the picture read
// between pixels and the blend kept when it stays in the edge's range
const char *kFxaa = R"(#version 120
uniform sampler2D uColor;
uniform vec2 uSize;
void main() {
  vec2 px = 1.0 / uSize, uv = gl_FragCoord.xy * px;
  vec3 nw = texture2D(uColor, uv + vec2(-1.0, -1.0) * px).rgb;
  vec3 ne = texture2D(uColor, uv + vec2(1.0, -1.0) * px).rgb;
  vec3 sw = texture2D(uColor, uv + vec2(-1.0, 1.0) * px).rgb;
  vec3 se = texture2D(uColor, uv + vec2(1.0, 1.0) * px).rgb;
  vec3 m = texture2D(uColor, uv).rgb;
  vec3 luma = vec3(0.299, 0.587, 0.114);
  float lNW = dot(nw, luma), lNE = dot(ne, luma), lSW = dot(sw, luma), lSE = dot(se, luma), lM = dot(m, luma);
  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
  float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
  vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
  float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * (1.0 / 8.0), 1.0 / 128.0);
  float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
  dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * px;
  vec3 a = 0.5 * (texture2D(uColor, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture2D(uColor, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
  vec3 b = a * 0.5 + 0.25 * (texture2D(uColor, uv - dir * 0.5).rgb + texture2D(uColor, uv + dir * 0.5).rgb);
  float lB = dot(b, luma);
  gl_FragColor = vec4((lB < lMin || lB > lMax) ? a : b, 1.0);
}
)";

// Bloom: what's brighter than a threshold, gathered from two rings around
// each pixel, added back over it
const char *kBloom = R"(#version 120
uniform sampler2D uColor;
uniform vec2 uSize;
uniform float strength;
vec3 bright(vec2 uv) {
  vec3 c = texture2D(uColor, uv).rgb;
  return max(c - vec3(0.68), vec3(0.0));
}
void main() {
  vec2 px = 1.0 / uSize, uv = gl_FragCoord.xy * px;
  vec3 c = texture2D(uColor, uv).rgb, glow = vec3(0.0);
  for (int i = 0; i < 12; i++) {
    float a = float(i) * 0.5236;
    vec2 d = vec2(cos(a), sin(a));
    glow += bright(uv + d * 4.0 * px) * 0.6 + bright(uv + d * 9.0 * px) * 0.4;
  }
  gl_FragColor = vec4(clamp(c + glow / 12.0 * strength * 2.2, 0.0, 1.0), 1.0);
}
)";

// Tilt-shift: away from a band across the middle, the picture blurred more
// the further from it (a disc of taps), the miniature-model look
const char *kTiltShift = R"(#version 120
uniform sampler2D uColor;
uniform vec2 uSize;
uniform float strength;
void main() {
  vec2 px = 1.0 / uSize, uv = gl_FragCoord.xy * px;
  float away = smoothstep(0.18, 0.5, abs(uv.y - 0.5));
  float r = away * strength * 6.0;
  vec3 c = texture2D(uColor, uv).rgb;
  if (r < 0.25) {
    gl_FragColor = vec4(c, 1.0);
    return;
  }
  vec3 sum = c;
  float n = 1.0;
  for (int i = 0; i < 16; i++) {
    float a = float(i) * 2.39996;
    float d = sqrt(float(i) + 0.5) / 4.0;
    sum += texture2D(uColor, uv + vec2(cos(a), sin(a)) * d * r * px).rgb;
    n += 1.0;
  }
  gl_FragColor = vec4(sum / n, 1.0);
}
)";

// The grade: contrast, vibrance (the duller colours lifted most), a
// vignette and a fine moving grain
const char *kGrade = R"(#version 120
uniform sampler2D uColor;
uniform vec2 uSize;
uniform float uTime;
uniform float contrast;
uniform float vibrance;
uniform float vignette;
uniform float grain;
void main() {
  vec2 uv = gl_FragCoord.xy / uSize;
  vec3 c = texture2D(uColor, uv).rgb;
  float l = dot(c, vec3(0.299, 0.587, 0.114));
  float sat = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
  c = mix(vec3(l), c, 1.0 + vibrance * (1.0 - sat));
  c = (c - 0.5) * (1.0 + contrast) + 0.5;
  vec2 q = uv - 0.5;
  c *= 1.0 - vignette * smoothstep(0.35, 0.85, length(q * vec2(1.0, uSize.y / uSize.x) * 1.4));
  float n = fract(sin(dot(gl_FragCoord.xy + floor(uTime * 24.0) * 37.0, vec2(12.9898, 78.233))) * 43758.5453);
  c += (n - 0.5) * grain;
  gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

struct Pass {
  const char *source;
  unsigned program = 0;
  bool tried = false;
};
Pass g_fxaa{kFxaa}, g_bloom{kBloom}, g_tilt{kTiltShift}, g_grade{kGrade};

unsigned programOf(Pass &p) {
  if (!p.tried) {
    p.tried = true;
    std::string log;
    p.program = GpuFsr::compileShader(p.source, log);
    if (!p.program)
      SDL_Log("[GraphicsEffects] a shader didn't compile: %s", log.c_str());
  }
  return p.program;
}

void ensure() {
  if (!g_loaded)
    load();
}

} // namespace

const Settings &settings() {
  ensure();
  return g_settings;
}

void load() {
  g_loaded = true;
  g_settings.fxaa = ::Settings::getInt("user", "fxaa", 0) != 0;
  g_settings.bloom = std::clamp(::Settings::getInt("user", "bloom", 0), 0, 2);
  g_settings.tiltShift = std::clamp(::Settings::getInt("user", "tiltshift", 0), 0, 2);
  g_settings.contrast = std::clamp(::Settings::getInt("user", "contrast", 0), 0, 100);
  g_settings.vibrance = std::clamp(::Settings::getInt("user", "vibrance", 0), 0, 100);
  g_settings.vignette = ::Settings::getInt("user", "vignette", 0) != 0;
  g_settings.grain = ::Settings::getInt("user", "grain", 0) != 0;
}

void set(const Settings &s) {
  ensure();
  g_settings = s;
  ::Settings::setInt("user", "fxaa", s.fxaa ? 1 : 0);
  ::Settings::setInt("user", "bloom", s.bloom);
  ::Settings::setInt("user", "tiltshift", s.tiltShift);
  ::Settings::setInt("user", "contrast", s.contrast);
  ::Settings::setInt("user", "vibrance", s.vibrance);
  ::Settings::setInt("user", "vignette", s.vignette ? 1 : 0);
  ::Settings::setInt("user", "grain", s.grain ? 1 : 0);
}

bool anyOn() {
  const Settings &s = settings();
  return s.fxaa || s.bloom || s.tiltShift || s.contrast || s.vibrance || s.vignette || s.grain;
}

void applyMap(SDL_Renderer *renderer) {
  if (!renderer || !anyOn() || !GpuFsr::available())
    return;
  const Settings &s = g_settings;
  if (s.fxaa)
    if (unsigned p = programOf(g_fxaa))
      GpuFsr::screenPass(renderer, p, {}, true);
  if (s.bloom)
    if (unsigned p = programOf(g_bloom))
      GpuFsr::screenPass(renderer, p, {{"strength", s.bloom == 2 ? 1.0f : 0.5f}}, true);
  if (s.tiltShift)
    if (unsigned p = programOf(g_tilt))
      GpuFsr::screenPass(renderer, p, {{"strength", s.tiltShift == 2 ? 1.0f : 0.55f}}, true);
  if (s.contrast || s.vibrance || s.vignette || s.grain)
    if (unsigned p = programOf(g_grade))
      GpuFsr::screenPass(renderer, p,
                         {{"contrast", s.contrast / 100.0f * 0.5f},
                          {"vibrance", s.vibrance / 100.0f},
                          {"vignette", s.vignette ? 0.35f : 0.0f},
                          {"grain", s.grain ? 0.045f : 0.0f}});
}

} // namespace GraphicsEffects
