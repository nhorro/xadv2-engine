// Omnilight — a point light that adds energy to the texture in a soft falloff
// (instead of darkening outside, like the spotlight). Use for a candle, lamp,
// torch, or any glowing source painted into the background that should bleed
// onto things drawn nearby (regions stacked on a graded layer pick it up too,
// thanks to issue #105's region-shader inheritance).
//
// Reserved uniforms: `texture` (sampler), `u_resolution` (vec2). `u_time` is
// unused — combine with the candle_flicker shader as a second pass if you want
// wobble.
//
// Author params:
//   center    : vec2, in pixels of the drawable's texture
//   radius    : float, falloff radius (pixels)
//   color     : vec3, light colour (e.g. [1.0, 0.85, 0.5] for warm)
//   intensity : float, peak boost at the center (0 = off, 1 = strong)
//
// Wire:
//   shader:
//     source: shaders/omnilight.frag
//     params: { center: [320, 200], radius: 220, color: [1.0, 0.8, 0.5], intensity: 0.6 }

uniform sampler2D texture;
uniform vec2  u_resolution;
uniform vec2  center;
uniform float radius;
uniform vec3  color;
uniform float intensity;

void main() {
    vec4 px = texture2D(texture, gl_TexCoord[0].xy);
    vec2 fragpx = gl_TexCoord[0].xy * u_resolution;
    float d = distance(fragpx, center);
    // 1 at the center, 0 at the edge — quadratic-ish falloff for a soft pool.
    float k = 1.0 - smoothstep(0.0, max(radius, 0.001), d);
    k = k * k * intensity;
    vec3 lit = px.rgb + color * k;
    gl_FragColor = vec4(lit, px.a) * gl_Color;
}
