#include "GpuFsr.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <SDL2/SDL_opengl.h>

// The shaders are the float path of ffx_fsr1.h, written for GLSL 1.20:
//   Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//   MIT license. https://github.com/GPUOpen-Effects/FidelityFX-FSR

namespace GpuFsr {

// ----------------------------------------------------------------------------
// OpenGL entry points (loaded through SDL, as SDL's own renderer does)
// ----------------------------------------------------------------------------
struct Gl {
  void(APIENTRY *Viewport)(GLint, GLint, GLsizei, GLsizei);
  void(APIENTRY *Begin)(GLenum);
  void(APIENTRY *End)();
  void(APIENTRY *Vertex2f)(GLfloat, GLfloat);
  void(APIENTRY *GetIntegerv)(GLenum, GLint *);
  GLboolean(APIENTRY *IsEnabled)(GLenum);
  void(APIENTRY *Enable)(GLenum);
  void(APIENTRY *Disable)(GLenum);
  PFNGLBLENDFUNCSEPARATEPROC BlendFuncSeparate;
  PFNGLCREATESHADERPROC CreateShader;
  PFNGLSHADERSOURCEPROC ShaderSource;
  PFNGLCOMPILESHADERPROC CompileShader;
  PFNGLGETSHADERIVPROC GetShaderiv;
  PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
  PFNGLCREATEPROGRAMPROC CreateProgram;
  PFNGLATTACHSHADERPROC AttachShader;
  PFNGLLINKPROGRAMPROC LinkProgram;
  PFNGLGETPROGRAMIVPROC GetProgramiv;
  PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
  PFNGLUSEPROGRAMPROC UseProgram;
  PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
  PFNGLUNIFORM1IPROC Uniform1i;
  PFNGLUNIFORM1FPROC Uniform1f;
  PFNGLUNIFORM2FPROC Uniform2f;
  PFNGLUNIFORM4FPROC Uniform4f;
  void(APIENTRY *CopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
  PFNGLACTIVETEXTUREPROC ActiveTexture;
};

static Gl gl;
static bool g_available = false;
static bool g_upscaling = true; // (FSR picked for the art)
static float g_con = 1.0f; // RCAS sharpness, as 2^-stops
static GLuint g_easu = 0, g_rcas = 0, g_colour = 0, g_style = 0;
static bool g_styled = false; // (Stylized picked: kStyle in RCAS's place)
// The frame's colour pass: a 3x3 matrix (rows) and a gamma
static float g_matrix[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
static float g_gamma = 1.0f;
static bool g_colourOn = false;
static SDL_Texture *g_frame = nullptr; // (the frame copied for it)
static int g_frameW = 0, g_frameH = 0;

struct LayerTargets {
  SDL_Texture *low = nullptr; // the art at 1:1
  int lowW = 0, lowH = 0;
  SDL_Texture *mid = nullptr; // EASU output, window-sized
  int midW = 0, midH = 0;
};
static LayerTargets g_layers[2];

template <typename T> static bool load(T &fn, const char *name) {
  fn = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
  if (!fn)
    SDL_Log("[GpuFsr] missing OpenGL function %s", name);
  return fn != nullptr;
}

static bool loadGl() {
  bool ok = true;
  ok &= load(gl.Viewport, "glViewport");
  ok &= load(gl.Begin, "glBegin");
  ok &= load(gl.End, "glEnd");
  ok &= load(gl.Vertex2f, "glVertex2f");
  ok &= load(gl.GetIntegerv, "glGetIntegerv");
  ok &= load(gl.IsEnabled, "glIsEnabled");
  ok &= load(gl.Enable, "glEnable");
  ok &= load(gl.Disable, "glDisable");
  ok &= load(gl.BlendFuncSeparate, "glBlendFuncSeparate");
  ok &= load(gl.CreateShader, "glCreateShader");
  ok &= load(gl.ShaderSource, "glShaderSource");
  ok &= load(gl.CompileShader, "glCompileShader");
  ok &= load(gl.GetShaderiv, "glGetShaderiv");
  ok &= load(gl.GetShaderInfoLog, "glGetShaderInfoLog");
  ok &= load(gl.CreateProgram, "glCreateProgram");
  ok &= load(gl.AttachShader, "glAttachShader");
  ok &= load(gl.LinkProgram, "glLinkProgram");
  ok &= load(gl.GetProgramiv, "glGetProgramiv");
  ok &= load(gl.GetProgramInfoLog, "glGetProgramInfoLog");
  ok &= load(gl.UseProgram, "glUseProgram");
  ok &= load(gl.GetUniformLocation, "glGetUniformLocation");
  ok &= load(gl.Uniform1i, "glUniform1i");
  ok &= load(gl.Uniform1f, "glUniform1f");
  ok &= load(gl.Uniform2f, "glUniform2f");
  ok &= load(gl.Uniform4f, "glUniform4f");
  ok &= load(gl.CopyTexSubImage2D, "glCopyTexSubImage2D");
  // (for mods' shaders: two textures at once - optional, FSR doesn't need it)
  gl.ActiveTexture = reinterpret_cast<PFNGLACTIVETEXTUREPROC>(SDL_GL_GetProcAddress("glActiveTexture"));
  return ok;
}

// ----------------------------------------------------------------------------
// Shaders. Positions are in pixels with (0,0) at the top left; flipY turns
// the window's bottom-up rows around (SDL's render targets are top-down).
// ----------------------------------------------------------------------------
static const char *kVertex = R"(
#version 120
void main() { gl_Position = gl_Vertex; }
)";

static const char *kEasu = R"(
#version 120
uniform sampler2D src;
uniform vec2 texSize; // source texture, pixels
uniform vec2 inView;  // the part of it being upscaled, from (0,0)
uniform vec4 dst;     // output rect: x, y, w, h
uniform float outH;   // height of the framebuffer drawn to
uniform float flipY;

vec4 tap(vec2 p) {
  p = clamp(p, vec2(0.0), inView - 1.0);
  return texture2D(src, (p + 0.5) / texSize);
}
float luma(vec4 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }

// Direction and length of the edge, from the '+' around c:
//    a
//  b c d
//    e
void easuSet(inout vec2 dir, inout float len, float w,
             float lA, float lB, float lC, float lD, float lE) {
  float lenX = max(abs(lD - lC), abs(lC - lB));
  float dX = lD - lB;
  dir.x += dX * w;
  lenX = lenX > 0.0 ? clamp(abs(dX) / lenX, 0.0, 1.0) : 0.0;
  len += lenX * lenX * w;
  float lenY = max(abs(lE - lC), abs(lC - lA));
  float dY = lE - lA;
  dir.y += dY * w;
  lenY = lenY > 0.0 ? clamp(abs(dY) / lenY, 0.0, 1.0) : 0.0;
  len += lenY * lenY * w;
}

// One tap of the edge-shaped Lanczos-2 approximation
void easuTap(inout vec4 aC, inout float aW, vec2 off, vec2 dir, vec2 len2,
             float lob, float clp, vec4 c) {
  vec2 v = vec2(off.x * dir.x + off.y * dir.y,
                off.x * -dir.y + off.y * dir.x) * len2;
  float d2 = min(dot(v, v), clp);
  float wB = 0.4 * d2 - 1.0;
  float wA = lob * d2 - 1.0;
  wB *= wB;
  wA *= wA;
  wB = 1.5625 * wB - 0.5625;
  float w = wB * wA;
  aC += c * w;
  aW += w;
}

void main() {
  vec2 frag = gl_FragCoord.xy;
  if (flipY > 0.5)
    frag.y = outH - frag.y;
  vec2 pp = (frag - dst.xy) * (inView / dst.zw) - 0.5;
  vec2 fp = floor(pp);
  pp -= fp;

  // 12-tap kernel
  //    b c
  //  e f g h
  //  i j k l
  //    n o
  vec4 b = tap(fp + vec2(0.0, -1.0)), c = tap(fp + vec2(1.0, -1.0));
  vec4 e = tap(fp + vec2(-1.0, 0.0)), f = tap(fp);
  vec4 g = tap(fp + vec2(1.0, 0.0)), h = tap(fp + vec2(2.0, 0.0));
  vec4 i = tap(fp + vec2(-1.0, 1.0)), j = tap(fp + vec2(0.0, 1.0));
  vec4 k = tap(fp + vec2(1.0, 1.0)), l = tap(fp + vec2(2.0, 1.0));
  vec4 n = tap(fp + vec2(0.0, 2.0)), o = tap(fp + vec2(1.0, 2.0));
  float bL = luma(b), cL = luma(c), eL = luma(e), fL = luma(f), gL = luma(g),
        hL = luma(h), iL = luma(i), jL = luma(j), kL = luma(k), lL = luma(l),
        nL = luma(n), oL = luma(o);

  vec2 dir = vec2(0.0);
  float len = 0.0;
  easuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
  easuSet(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
  easuSet(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
  easuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);

  float dirR = dot(dir, dir);
  if (dirR < 1.0 / 32768.0) {
    dirR = 1.0;
    dir.x = 1.0;
  } else {
    dirR = inversesqrt(dirR);
  }
  dir *= dirR;
  len = len * 0.5;
  len *= len;
  float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));
  vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
  float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
  float clp = 1.0 / lob;

  vec4 aC = vec4(0.0);
  float aW = 0.0;
  easuTap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
  easuTap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);
  easuTap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
  easuTap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
  easuTap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
  easuTap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
  easuTap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
  easuTap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
  easuTap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
  easuTap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
  easuTap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, o);
  easuTap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);

  // Normalize and dering against the 4 nearest
  vec4 mn = min(min(f, g), min(j, k));
  vec4 mx = max(max(f, g), max(j, k));
  vec4 res = aW != 0.0 ? aC / aW : f;
  gl_FragColor = clamp(res, mn, mx);
}
)";

static const char *kRcas = R"(
#version 120
uniform sampler2D src;
uniform vec2 texSize;
uniform vec4 dst;
uniform float outH;
uniform float flipY;
uniform float con; // sharpness, 2^-stops

// The buffer holds the upscaled layer at dst's place
vec4 tap(vec2 p) {
  p = clamp(p, vec2(0.0), dst.zw - 1.0);
  return texture2D(src, (dst.xy + p + 0.5) / texSize);
}

void main() {
  vec2 frag = gl_FragCoord.xy;
  if (flipY > 0.5)
    frag.y = outH - frag.y;
  vec2 p = floor(frag - dst.xy);
  //    b
  //  d e f
  //    h
  vec4 b = tap(p + vec2(0.0, -1.0)), d = tap(p + vec2(-1.0, 0.0));
  vec4 e = tap(p), f = tap(p + vec2(1.0, 0.0)), h = tap(p + vec2(0.0, 1.0));
  vec3 mn4 = min(min(b.rgb, d.rgb), min(f.rgb, h.rgb));
  vec3 mx4 = max(max(b.rgb, d.rgb), max(f.rgb, h.rgb));
  vec3 hitMin = min(mn4, e.rgb) / max(4.0 * mx4, vec3(1e-6));
  vec3 hitMax = (1.0 - max(mx4, e.rgb)) / min(4.0 * mn4 - 4.0, vec3(-1e-6));
  vec3 lobeRGB = max(-hitMin, hitMax);
  float lobe = max(-(0.25 - 1.0 / 16.0),
                   min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * con;
  vec4 o;
  o.rgb = (lobe * (b.rgb + d.rgb + h.rgb + f.rgb) + e.rgb) / (4.0 * lobe + 1.0);
  o.a = e.a;
  o.rgb = clamp(o.rgb, vec3(0.0), vec3(o.a)); // stays premultiplied
  gl_FragColor = o;
}
)";

// Stylized (in RCAS's place, on EASU's smooth-edged upscale): the look of
// cel-shaded line art, after Anime4K's (bloc97, MIT) line darkening and
// thinning. Along an edge (a Sobel gradient of the brightness) a pixel darker
// than its neighbours goes to the darkest of them - the outlines dark,
// crisp and steady - one lighter is sharpened against its neighbours, and
// the colours are lifted (vibrance: the dull ones most, skin and sky not
// blown out). strength: 0 to 1, from Art Sharpness
static const char *kStyle = R"(
#version 120
uniform sampler2D src;
uniform vec2 texSize;
uniform vec4 dst;
uniform float outH;
uniform float flipY;
uniform float strength;

vec4 tap(vec2 p) {
  p = clamp(p, vec2(0.0), dst.zw - 1.0);
  return texture2D(src, (dst.xy + p + 0.5) / texSize);
}
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec2 frag = gl_FragCoord.xy;
  if (flipY > 0.5)
    frag.y = outH - frag.y;
  vec2 p = floor(frag - dst.xy);
  // a b c
  // d e f
  // g h i
  vec4 a = tap(p + vec2(-1.0, -1.0)), b = tap(p + vec2(0.0, -1.0)), c = tap(p + vec2(1.0, -1.0));
  vec4 d = tap(p + vec2(-1.0, 0.0)), e = tap(p), f = tap(p + vec2(1.0, 0.0));
  vec4 g = tap(p + vec2(-1.0, 1.0)), h = tap(p + vec2(0.0, 1.0)), i = tap(p + vec2(1.0, 1.0));
  float la = luma(a.rgb), lb = luma(b.rgb), lc = luma(c.rgb), ld = luma(d.rgb), le = luma(e.rgb);
  float lf = luma(f.rgb), lg = luma(g.rgb), lh = luma(h.rgb), li = luma(i.rgb);
  float gx = (lc + 2.0 * lf + li) - (la + 2.0 * ld + lg);
  float gy = (lg + 2.0 * lh + li) - (la + 2.0 * lb + lc);
  float edge = clamp(length(vec2(gx, gy)) * 2.0, 0.0, 1.0);
  vec3 mean = (a.rgb + b.rgb + c.rgb + d.rgb + f.rgb + g.rgb + h.rgb + i.rgb) / 8.0;
  vec3 mn = min(min(min(a.rgb, b.rgb), min(c.rgb, d.rgb)), min(min(f.rgb, g.rgb), min(h.rgb, i.rgb)));
  float darker = clamp((luma(mean) - le) * 10.0, 0.0, 1.0);
  vec3 o = e.rgb;
  // the outline: to the darkest round it
  o = mix(o, min(o, mn), edge * darker * strength);
  // the rest: sharper against its neighbours
  o += (e.rgb - mean) * 0.6 * strength * (1.0 - darker);
  // vibrance (premultiplied colours: measured unpremultiplied)
  float al = max(e.a, 1e-4);
  vec3 u = clamp(o / al, 0.0, 1.0);
  float sat = max(u.r, max(u.g, u.b)) - min(u.r, min(u.g, u.b));
  float lu = luma(u);
  u = mix(vec3(lu), u, 1.0 + 0.55 * strength * (1.0 - sat));
  // and a touch more contrast round the middle
  u = clamp((u - 0.5) * (1.0 + 0.12 * strength) + 0.5, 0.0, 1.0);
  gl_FragColor = vec4(u * e.a, e.a);
}
)";

// The frame's colours: each pixel times a 3x3 matrix, then a gamma. The
// frame is copied from the window, whose rows and the copy's both run
// bottom-up, so a pixel reads its own place
static const char *kColour = R"(
#version 120
uniform sampler2D src;
uniform vec2 size;
uniform vec4 r0;
uniform vec4 r1;
uniform vec4 r2;
uniform float gamma;
void main() {
  vec3 c = texture2D(src, gl_FragCoord.xy / size).rgb;
  vec3 o = clamp(vec3(dot(r0.xyz, c), dot(r1.xyz, c), dot(r2.xyz, c)), 0.0, 1.0);
  gl_FragColor = vec4(pow(o, vec3(1.0 / gamma)), 1.0);
}
)";

static GLuint compile(GLenum type, const char *source, const char *what) {
  GLuint s = gl.CreateShader(type);
  gl.ShaderSource(s, 1, &source, nullptr);
  gl.CompileShader(s);
  GLint ok = 0;
  gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048] = {};
    gl.GetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
    SDL_Log("[GpuFsr] %s shader failed to compile: %s", what, log);
    return 0;
  }
  return s;
}

static GLuint link(const char *fragment, const char *what) {
  GLuint vs = compile(GL_VERTEX_SHADER, kVertex, what);
  GLuint fs = compile(GL_FRAGMENT_SHADER, fragment, what);
  if (!vs || !fs)
    return 0;
  GLuint p = gl.CreateProgram();
  gl.AttachShader(p, vs);
  gl.AttachShader(p, fs);
  gl.LinkProgram(p);
  GLint ok = 0;
  gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048] = {};
    gl.GetProgramInfoLog(p, sizeof(log) - 1, nullptr, log);
    SDL_Log("[GpuFsr] %s program failed to link: %s", what, log);
    return 0;
  }
  return p;
}

bool init(SDL_Renderer *renderer, float sharpness) {
  g_available = false;
  g_con = std::exp2(-std::max(0.0f, sharpness));
  SDL_RendererInfo info;
  if (!renderer || SDL_GetRendererInfo(renderer, &info) != 0 ||
      std::strcmp(info.name, "opengl") != 0) {
    SDL_Log("[GpuFsr] needs the OpenGL renderer (have %s)",
            renderer ? info.name : "none");
    return false;
  }
  if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || !loadGl())
    return false;

  // Make the renderer's context current (binding any texture does that)
  SDL_Texture *probe = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_TARGET, 4, 4);
  if (!probe)
    return false;
  float tw = 0, th = 0;
  SDL_GL_BindTexture(probe, &tw, &th);
  bool normalized = std::fabs(tw - 1.0f) < 0.001f; // GL_TEXTURE_2D
  SDL_DestroyTexture(probe);
  if (!normalized) {
    SDL_Log("[GpuFsr] rectangle textures are not supported");
    return false;
  }

  g_easu = link(kEasu, "EASU");
  g_rcas = link(kRcas, "RCAS");
  g_colour = link(kColour, "colour");
  g_style = link(kStyle, "stylized");
  g_available = g_easu && g_rcas;
  SDL_Log("[GpuFsr] GPU FSR 1 %s (sharpness %.2f)",
          g_available ? "ready" : "unavailable", sharpness);
  return g_available;
}

bool available() { return g_available; }

void setSharpness(float sharpness) { g_con = std::exp2(-std::max(0.0f, sharpness)); }

static SDL_Texture *target(SDL_Renderer *renderer, SDL_Texture *&tex, int &tw, int &th, int w, int h);
static void fullQuad();

bool upscales(float scale) { return g_available && g_upscaling && scale > 1.001f; }

void setUpscaling(bool on) { g_upscaling = on; }

void setStyled(bool on) { g_styled = on; }

void setColour(int filter, float strength, float brightness) {
  // A viewer's sight of each colour (Machado et al. 2009, severity 1), and
  // where the difference they miss is moved to (Fidaner et al.: red-green
  // into green and blue; blue-yellow into red and green)
  static const float sim[4][3][3] = {
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
      {{0.152286f, 1.052583f, -0.204868f}, {0.114503f, 0.786281f, 0.099216f}, {-0.003882f, -0.048116f, 1.051998f}},
      {{0.367322f, 0.860646f, -0.227968f}, {0.280085f, 0.672501f, 0.047413f}, {-0.011820f, 0.042940f, 0.968881f}},
      {{1.255528f, -0.076749f, -0.178779f}, {-0.078411f, 0.930809f, 0.147602f}, {0.004733f, 0.691367f, 0.303900f}}};
  static const float shiftRG[3][3] = {{0, 0, 0}, {0.7f, 1, 0}, {0.7f, 0, 1}};
  static const float shiftBY[3][3] = {{1, 0, 0.7f}, {0, 1, 0.7f}, {0, 0, 0}};
  filter = std::clamp(filter, 0, 3);
  strength = std::clamp(strength, 0.0f, 1.0f);
  const float(*shift)[3] = filter == 3 ? shiftBY : shiftRG;
  // M = I + strength * shift * (I - sim)
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++) {
      float e = 0.0f;
      for (int k = 0; k < 3; k++)
        e += shift[r][k] * ((k == c ? 1.0f : 0.0f) - sim[filter][k][c]);
      g_matrix[r][c] = (r == c ? 1.0f : 0.0f) + (filter ? strength * e : 0.0f);
    }
  g_gamma = std::clamp(brightness, 0.25f, 4.0f);
  g_colourOn = (filter != 0 && strength > 0.0f) || std::fabs(g_gamma - 1.0f) > 0.001f;
}

void finishFrame(SDL_Renderer *renderer) {
  if (!g_available || !g_colour || !g_colourOn || !renderer)
    return;
  SDL_Texture *was = SDL_GetRenderTarget(renderer);
  SDL_SetRenderTarget(renderer, nullptr);
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(renderer, &w, &h);
  if (w <= 0 || h <= 0 || !target(renderer, g_frame, g_frameW, g_frameH, w, h)) {
    SDL_SetRenderTarget(renderer, was);
    return;
  }
  SDL_RenderFlush(renderer);
  GLint program = 0;
  gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);
  GLboolean blendOn = gl.IsEnabled(GL_BLEND);
  GLboolean scissorOn = gl.IsEnabled(GL_SCISSOR_TEST);
  gl.Disable(GL_SCISSOR_TEST);
  gl.Disable(GL_BLEND);
  // The window's frame, as drawn, into the copy
  SDL_GL_BindTexture(g_frame, nullptr, nullptr);
  gl.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
  // and back over the whole window through the colour pass
  gl.Viewport(0, 0, w, h);
  gl.UseProgram(g_colour);
  gl.Uniform1i(gl.GetUniformLocation(g_colour, "src"), 0);
  gl.Uniform2f(gl.GetUniformLocation(g_colour, "size"), float(w), float(h));
  gl.Uniform4f(gl.GetUniformLocation(g_colour, "r0"), g_matrix[0][0], g_matrix[0][1], g_matrix[0][2], 0.0f);
  gl.Uniform4f(gl.GetUniformLocation(g_colour, "r1"), g_matrix[1][0], g_matrix[1][1], g_matrix[1][2], 0.0f);
  gl.Uniform4f(gl.GetUniformLocation(g_colour, "r2"), g_matrix[2][0], g_matrix[2][1], g_matrix[2][2], 0.0f);
  gl.Uniform1f(gl.GetUniformLocation(g_colour, "gamma"), g_gamma);
  fullQuad();
  gl.UseProgram(static_cast<GLuint>(program));
  if (blendOn)
    gl.Enable(GL_BLEND);
  if (scissorOn)
    gl.Enable(GL_SCISSOR_TEST);
  SDL_SetRenderTarget(renderer, was);
}

// ---------------------------------------------------------------- shaders
// Mods' own shaders (ShaderMods.hpp): a fragment shader of theirs, linked
// with our full-window vertex shader
unsigned compileShader(const std::string &fragment, std::string &log) {
  log.clear();
  if (!g_available)
    return log = "needs OpenGL on the graphics card", 0;
  const char *src = fragment.c_str();
  GLuint vs = compile(GL_VERTEX_SHADER, kVertex, "mod vertex");
  GLuint fs = gl.CreateShader(GL_FRAGMENT_SHADER);
  gl.ShaderSource(fs, 1, &src, nullptr);
  gl.CompileShader(fs);
  GLint ok = 0;
  gl.GetShaderiv(fs, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char b[4096] = {};
    gl.GetShaderInfoLog(fs, sizeof(b) - 1, nullptr, b);
    log = b;
    return 0;
  }
  GLuint p = gl.CreateProgram();
  gl.AttachShader(p, vs);
  gl.AttachShader(p, fs);
  gl.LinkProgram(p);
  gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char b[4096] = {};
    gl.GetProgramInfoLog(p, sizeof(b) - 1, nullptr, b);
    log = b;
    return 0;
  }
  return p;
}

namespace {
// The renderer's viewport, clip and scale - in the target's own pixels -
// kept while a pass switches targets, put back after (SDL keeps them for
// the window only)
struct Kept {
  SDL_Texture *target;
  float sx, sy;
  SDL_Rect view, clip;
  bool clipped;
  void keep(SDL_Renderer *r) {
    target = SDL_GetRenderTarget(r);
    SDL_RenderGetScale(r, &sx, &sy);
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    SDL_RenderGetViewport(r, &view);
    clipped = SDL_RenderIsClipEnabled(r);
    SDL_RenderGetClipRect(r, &clip);
    SDL_RenderSetScale(r, sx, sy);
  }
  void restore(SDL_Renderer *r) {
    SDL_SetRenderTarget(r, target);
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    SDL_RenderSetViewport(r, &view);
    SDL_RenderSetClipRect(r, clipped ? &clip : nullptr);
    SDL_RenderSetScale(r, sx, sy);
  }
};
void setUniforms(GLuint prog, float w, float h, const std::vector<std::pair<std::string, float>> &u) {
  gl.Uniform2f(gl.GetUniformLocation(prog, "uSize"), w, h);
  gl.Uniform1f(gl.GetUniformLocation(prog, "uTime"), SDL_GetTicks() / 1000.0f);
  for (const auto &[name, v] : u)
    gl.Uniform1f(gl.GetUniformLocation(prog, name.c_str()), v);
}
} // namespace

bool artPass(SDL_Renderer *renderer, unsigned prog, SDL_Texture *color, SDL_Texture *aux, SDL_Texture *out, int w,
             int h, const std::vector<std::pair<std::string, float>> &uniforms) {
  if (!g_available || !prog || !color || !aux || !out || !gl.ActiveTexture)
    return false;
  Kept k;
  k.keep(renderer);
  SDL_SetRenderTarget(renderer, out);
  SDL_RenderFlush(renderer);
  GLint program = 0;
  gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);
  GLboolean blendOn = gl.IsEnabled(GL_BLEND), scissorOn = gl.IsEnabled(GL_SCISSOR_TEST);
  gl.Disable(GL_SCISSOR_TEST);
  gl.Disable(GL_BLEND);
  gl.Viewport(0, 0, w, h);
  // (the frame's colour on unit 0, its normals and height on unit 1; both
  // read texel for texel)
  SDL_SetTextureScaleMode(color, SDL_ScaleModeNearest);
  SDL_SetTextureScaleMode(aux, SDL_ScaleModeNearest);
  gl.ActiveTexture(GL_TEXTURE1);
  SDL_GL_BindTexture(aux, nullptr, nullptr);
  gl.ActiveTexture(GL_TEXTURE0);
  SDL_GL_BindTexture(color, nullptr, nullptr);
  gl.UseProgram(prog);
  gl.Uniform1i(gl.GetUniformLocation(prog, "uColor"), 0);
  gl.Uniform1i(gl.GetUniformLocation(prog, "uNormal"), 1);
  setUniforms(prog, float(w), float(h), uniforms);
  fullQuad();
  gl.UseProgram(static_cast<GLuint>(program));
  gl.ActiveTexture(GL_TEXTURE1);
  SDL_GL_UnbindTexture(aux);
  gl.ActiveTexture(GL_TEXTURE0);
  if (blendOn)
    gl.Enable(GL_BLEND);
  if (scissorOn)
    gl.Enable(GL_SCISSOR_TEST);
  k.restore(renderer);
  return true;
}

static SDL_Texture *g_screen = nullptr;
static int g_screenW = 0, g_screenH = 0;

bool screenPass(SDL_Renderer *renderer, unsigned prog, const std::vector<std::pair<std::string, float>> &uniforms) {
  if (!g_available || !prog || !renderer)
    return false;
  SDL_Texture *was = SDL_GetRenderTarget(renderer);
  SDL_SetRenderTarget(renderer, nullptr);
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(renderer, &w, &h);
  if (w <= 0 || h <= 0 || !target(renderer, g_screen, g_screenW, g_screenH, w, h)) {
    SDL_SetRenderTarget(renderer, was);
    return false;
  }
  SDL_RenderFlush(renderer);
  GLint program = 0;
  gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);
  GLboolean blendOn = gl.IsEnabled(GL_BLEND), scissorOn = gl.IsEnabled(GL_SCISSOR_TEST);
  gl.Disable(GL_SCISSOR_TEST);
  gl.Disable(GL_BLEND);
  // (the window's frame copied - its rows bottom-up, as the copy's: a
  // pixel reads its own place at gl_FragCoord / uSize)
  SDL_GL_BindTexture(g_screen, nullptr, nullptr);
  gl.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
  gl.Viewport(0, 0, w, h);
  gl.UseProgram(prog);
  gl.Uniform1i(gl.GetUniformLocation(prog, "uColor"), 0);
  setUniforms(prog, float(w), float(h), uniforms);
  fullQuad();
  gl.UseProgram(static_cast<GLuint>(program));
  if (blendOn)
    gl.Enable(GL_BLEND);
  if (scissorOn)
    gl.Enable(GL_SCISSOR_TEST);
  SDL_SetRenderTarget(renderer, was);
  return true;
}

// A render target of exactly w x h, made or remade as needed
static SDL_Texture *target(SDL_Renderer *renderer, SDL_Texture *&tex, int &tw,
                           int &th, int w, int h) {
  if (tex && tw == w && th == h)
    return tex;
  if (tex)
    SDL_DestroyTexture(tex);
  tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_TARGET, w, h);
  tw = w;
  th = h;
  if (tex) {
    // FSR reads exact pixels
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
  }
  return tex;
}

void beginLayer(SDL_Renderer *renderer, Layer layer, int lowW, int lowH,
                SDL_Color clear) {
  LayerTargets &lt = g_layers[static_cast<int>(layer)];
  if (!target(renderer, lt.low, lt.lowW, lt.lowH, std::max(1, lowW),
              std::max(1, lowH)))
    return;
  SDL_SetRenderTarget(renderer, lt.low);
  Uint8 r, g, b, a;
  SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
  SDL_BlendMode mode;
  SDL_GetRenderDrawBlendMode(renderer, &mode);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(renderer, clear.r, clear.g, clear.b, clear.a);
  SDL_RenderClear(renderer);
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
  SDL_SetRenderDrawBlendMode(renderer, mode);
}

static void fullQuad() {
  gl.Begin(GL_TRIANGLE_STRIP);
  gl.Vertex2f(-1.0f, -1.0f);
  gl.Vertex2f(1.0f, -1.0f);
  gl.Vertex2f(-1.0f, 1.0f);
  gl.Vertex2f(1.0f, 1.0f);
  gl.End();
}

void endLayer(SDL_Renderer *renderer, Layer layer, float viewW, float viewH,
              bool blend, const SDL_Rect *dstRect) {
  LayerTargets &lt = g_layers[static_cast<int>(layer)];
  if (!lt.low || SDL_GetRenderTarget(renderer) != lt.low)
    return;

  // Window size (asked with the window as the target)
  SDL_Texture *low = lt.low;
  SDL_SetRenderTarget(renderer, nullptr);
  int outW = 0, outH = 0;
  SDL_GetRendererOutputSize(renderer, &outW, &outH);
  if (outW <= 0 || outH <= 0 ||
      !target(renderer, lt.mid, lt.midW, lt.midH, outW, outH))
    return;
  // Where on the window the layer goes (top-down window pixels)
  SDL_Rect dst = dstRect ? *dstRect : SDL_Rect{0, 0, outW, outH};

  // Pass 1: EASU, the layer -> a window-sized buffer
  SDL_SetRenderTarget(renderer, lt.mid);
  SDL_RenderFlush(renderer);

  GLint program = 0, srcRgb = 0, dstRgb = 0, srcA = 0, dstA = 0;
  gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);
  gl.GetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
  gl.GetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
  gl.GetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
  gl.GetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
  GLboolean blendOn = gl.IsEnabled(GL_BLEND);
  GLboolean scissorOn = gl.IsEnabled(GL_SCISSOR_TEST);
  gl.Disable(GL_SCISSOR_TEST);
  gl.Disable(GL_BLEND);

  // SDL's render targets are top-down; only dst is drawn
  gl.Viewport(dst.x, dst.y, dst.w, dst.h);
  SDL_GL_BindTexture(low, nullptr, nullptr);
  gl.UseProgram(g_easu);
  gl.Uniform1i(gl.GetUniformLocation(g_easu, "src"), 0);
  gl.Uniform2f(gl.GetUniformLocation(g_easu, "texSize"), float(lt.lowW),
               float(lt.lowH));
  gl.Uniform2f(gl.GetUniformLocation(g_easu, "inView"),
               std::min(viewW, float(lt.lowW)), std::min(viewH, float(lt.lowH)));
  gl.Uniform4f(gl.GetUniformLocation(g_easu, "dst"), float(dst.x),
               float(dst.y), float(dst.w), float(dst.h));
  gl.Uniform1f(gl.GetUniformLocation(g_easu, "outH"), float(outH));
  gl.Uniform1f(gl.GetUniformLocation(g_easu, "flipY"), 0.0f);
  fullQuad();

  // Pass 2: RCAS, the buffer -> the window
  SDL_SetRenderTarget(renderer, nullptr);
  // The window is bottom-up
  gl.Viewport(dst.x, outH - dst.y - dst.h, dst.w, dst.h);
  if (blend) {
    // The layer is premultiplied (drawn over transparent black)
    gl.Enable(GL_BLEND);
    gl.BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                         GL_ONE_MINUS_SRC_ALPHA);
  }
  SDL_GL_BindTexture(lt.mid, nullptr, nullptr);
  // (Stylized: its pass in RCAS's place, its strength from the same slider)
  const GLuint pass = g_styled && g_style ? g_style : g_rcas;
  gl.UseProgram(pass);
  gl.Uniform1i(gl.GetUniformLocation(pass, "src"), 0);
  gl.Uniform2f(gl.GetUniformLocation(pass, "texSize"), float(outW),
               float(outH));
  gl.Uniform4f(gl.GetUniformLocation(pass, "dst"), float(dst.x),
               float(dst.y), float(dst.w), float(dst.h));
  gl.Uniform1f(gl.GetUniformLocation(pass, "outH"), float(outH));
  gl.Uniform1f(gl.GetUniformLocation(pass, "flipY"), 1.0f);
  if (pass == g_rcas)
    gl.Uniform1f(gl.GetUniformLocation(pass, "con"), g_con);
  else
    gl.Uniform1f(gl.GetUniformLocation(pass, "strength"), std::clamp(g_con * 1.15f, 0.0f, 1.0f));
  fullQuad();

  // Put back what SDL's renderer expects. The bound texture is left as
  // SDL_GL_BindTexture recorded it, and SDL resets the viewport itself.
  gl.UseProgram(static_cast<GLuint>(program));
  gl.BlendFuncSeparate(srcRgb, dstRgb, srcA, dstA);
  if (blendOn)
    gl.Enable(GL_BLEND);
  else
    gl.Disable(GL_BLEND);
  if (scissorOn)
    gl.Enable(GL_SCISSOR_TEST);
}

} // namespace GpuFsr
