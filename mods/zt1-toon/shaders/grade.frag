#version 120
// ZT1 Toon colour grade - a screen-stage shader mod: the whole finished
// picture with livelier colour and a little more contrast, the bright
// toon-game look. MIT licence, as the engine.

uniform sampler2D uColor;
uniform vec2 uSize;
uniform float strength;
uniform float contrast;

void main() {
  vec3 c = texture2D(uColor, gl_FragCoord.xy / uSize).rgb;
  float l = dot(c, vec3(0.299, 0.587, 0.114));
  float sat = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
  // (vibrance: the dull colours lifted most)
  c = mix(vec3(l), c, 1.0 + strength * (1.0 - sat));
  c = (c - 0.5) * (1.0 + contrast) + 0.5;
  gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
