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
};

static Gl gl;
static bool g_available = false;
static float g_con = 1.0f; // RCAS sharpness, as 2^-stops
static GLuint g_easu = 0, g_rcas = 0;

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
  g_available = g_easu && g_rcas;
  SDL_Log("[GpuFsr] GPU FSR 1 %s (sharpness %.2f)",
          g_available ? "ready" : "unavailable", sharpness);
  return g_available;
}

bool available() { return g_available; }

bool upscales(float scale) { return g_available && scale > 1.001f; }

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
  gl.UseProgram(g_rcas);
  gl.Uniform1i(gl.GetUniformLocation(g_rcas, "src"), 0);
  gl.Uniform2f(gl.GetUniformLocation(g_rcas, "texSize"), float(outW),
               float(outH));
  gl.Uniform4f(gl.GetUniformLocation(g_rcas, "dst"), float(dst.x),
               float(dst.y), float(dst.w), float(dst.h));
  gl.Uniform1f(gl.GetUniformLocation(g_rcas, "outH"), float(outH));
  gl.Uniform1f(gl.GetUniformLocation(g_rcas, "flipY"), 1.0f);
  gl.Uniform1f(gl.GetUniformLocation(g_rcas, "con"), g_con);
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
