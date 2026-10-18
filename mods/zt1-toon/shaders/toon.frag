#version 120
// ZT1 Toon - a toon (cel) shader for the map's art, an art-stage shader mod
// (src/ShaderMods.hpp). The art itself is the texture - its painting, fur
// and colours kept (upscaled: an HD art mod's frame when one is on) - and
// the usual toon model goes over it, as toon shaders for 3D characters
// light a textured model:
//   1. texture      - the art, its grain smoothed only as much as set
//   2. self shadow  - light against the surface (N.L, from the engine's
//                     normals) cut at a threshold, the cut's edge hard or soft
//   3. shadow colour- the shaded side the texture tinted and darkened
//   4. gloss        - a small highlight cut at a size
//   5. rim light    - where the surface turns away from the viewer
//   6. ink outline  - a clean, smooth line round the silhouette in a dark
//                     shade of the art's own colour, softer lines inside
//                     where the surface steps
// Toon Strength blends from the plain art to the full look.
// MIT licence, as the engine.

uniform sampler2D uColor;
uniform sampler2D uNormal;
uniform vec2 uSize;
uniform float uKind; // 0 animal or person, 1 fence, 2 object

uniform float toonStrength;
uniform float shadowThreshold;
uniform float shadowHardness;
uniform float shadowDarkness;
uniform float shadowTint;
uniform float lightAngle;
uniform float rimLight;
uniform float gloss;
uniform float outlineWidth;
uniform float outlineDarkness;
uniform float smoothGrain;
uniform float saturation;
// (the mod's own setting - mod.json "settings", Game Options > Mods &
// Updates > Settings: 1, only animals and people get the look)
uniform float peopleOnly;

vec4 colourAt(vec2 p) { return texture2D(uColor, (p + 0.5) / uSize); }
vec4 surfaceAt(vec2 p) { return texture2D(uNormal, (p + 0.5) / uSize); }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
// The body: solid, and not its shadow on the ground (near black in the art)
bool isBody(vec4 s) { return s.a >= 0.5 && s.r + s.g + s.b > 0.19; }
float bodyAt(vec2 p) { return isBody(colourAt(p)) ? 1.0 : 0.0; }

// The texture's grain smoothed round p, edges kept (a small bilateral blur)
vec3 smoothedAt(vec2 p, vec3 centre) {
  vec3 sum = vec3(0.0);
  float wsum = 0.0;
  for (int y = -2; y <= 2; y++)
    for (int x = -2; x <= 2; x++) {
      vec4 s = colourAt(p + vec2(float(x), float(y)));
      if (!isBody(s))
        continue;
      vec3 d = s.rgb - centre;
      float w = exp(-dot(d, d) / 0.01 - float(x * x + y * y) / 4.0);
      sum += s.rgb * w;
      wsum += w;
    }
  return wsum > 0.0 ? sum / wsum : centre;
}

void main() {
  vec2 p = floor(gl_FragCoord.xy);
  vec4 c = colourAt(p);
  if (peopleOnly > 0.5 && uKind != 0.0) {
    gl_FragColor = c;
    return;
  }
  // (its soft edge - see-through pixels round the outline - inked, as the
  // outline's outermost part, its see-through kept; its shadow on the ground
  // and anything else see-through left as drawn)
  if (c.a < 0.9 || !isBody(c)) {
    bool edge = c.a > 0.05 && c.r + c.g + c.b > 0.19 && uKind != 1.0;
    gl_FragColor = edge ? vec4(mix(c.rgb, c.rgb * 0.28, outlineDarkness * min(outlineWidth, 1.0)), c.a) : c;
    return;
  }

  // 1. The texture
  vec3 tex = mix(c.rgb, smoothedAt(p, c.rgb), smoothGrain);

  // 2. Self shadow
  vec3 N = normalize(surfaceAt(p).rgb * 2.0 - 1.0);
  float ang = radians(lightAngle);
  vec3 toLight = normalize(vec3(cos(ang), sin(ang), 0.85));
  float ndl = dot(N, toLight);
  float cut = mix(-0.6, 0.9, shadowThreshold);
  float soft = mix(0.3, 0.01, shadowHardness);
  float lit = smoothstep(cut - soft, cut + soft, ndl);
  // (fences and objects aren't rounded: lit mostly by their own painting)
  float shaped = uKind == 0.0 ? 1.0 : 0.25;
  lit = mix(1.0, lit, shaped);

  // 3. Shadow colour over the texture
  vec3 shade = mix(vec3(1.0), vec3(0.7, 0.75, 1.0), shadowTint) * (1.0 - shadowDarkness);
  vec3 col = tex * mix(shade, vec3(1.0), lit);

  // 4. Gloss and 5. rim light, lightly, in the texture's own colour
  vec3 halfway = normalize(toLight + vec3(0.0, 0.0, 1.0));
  float spec = smoothstep(0.95, 0.975, dot(N, halfway)) * gloss * lit * shaped;
  float rim = smoothstep(0.55, 0.85, 1.0 - N.z) * rimLight * shaped * mix(0.25, 1.0, lit);
  col += (spec + rim * 0.6) * mix(tex, vec3(1.0), 0.6) * 0.5;

  col = mix(tex, col, toonStrength);
  float l = luma(col);
  col = mix(vec3(l), col, saturation);

  // 6. Ink: how much of a ring round p (the line's width) is outside the
  // body - a smooth line, not a stepped one - and softer lines inside where
  // the surface's depth or direction steps
  // (in the art's own pixels, which come at 4x: the setting is about half a
  // screen pixel a step at the usual zoom)
  float width = outlineWidth * 2.0 * (uKind == 1.0 ? 0.3 : 1.0);
  // (rings at a third, two thirds and the whole width, the nearer weighing
  // more: dark at the edge, fading softly inward - a soft line, not a hard
  // band)
  float outside = 0.0;
  if (width > 0.05) {
    for (int i = 0; i < 16; i++) {
      float a = float(i) * 0.3927;
      vec2 o = vec2(cos(a), sin(a));
      outside += 0.5 * (1.0 - bodyAt(p + floor(o * width * 0.34 + 0.5)));
      outside += 0.3 * (1.0 - bodyAt(p + floor(o * width * 0.67 + 0.5)));
      outside += 0.2 * (1.0 - bodyAt(p + floor(o * width + 0.5)));
    }
    outside /= 16.0;
  }
  float ink = smoothstep(0.0, 0.45, outside);
  vec4 r = surfaceAt(p + vec2(2.0, 0.0)), lf = surfaceAt(p - vec2(2.0, 0.0));
  vec4 u = surfaceAt(p + vec2(0.0, 2.0)), d = surfaceAt(p - vec2(0.0, 2.0));
  float edgeStep = 2.0 * (abs(r.a - lf.a) + abs(u.a - d.a)) + length(r.rgb - lf.rgb) + length(u.rgb - d.rgb);
  float inner = uKind == 1.0 ? 0.0 : smoothstep(0.5, 0.9, edgeStep) * min(width / 3.0, 1.0);
  vec3 inkColour = tex * 0.28;
  col = mix(col, col * 0.7, inner * outlineDarkness * toonStrength);
  col = mix(col, inkColour, ink * outlineDarkness);

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), c.a);
}
