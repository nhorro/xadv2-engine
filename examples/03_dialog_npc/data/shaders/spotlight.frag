// Spotlight — a circular falloff darkening everything outside the pool of light,
// authored in texture-space (per-channel multiplier inside vs. outside). One of
// the basic shaders requested by issue #107; pairs naturally with `omnilight`
// when an effect's center is the same but the falloff/colour differs.
//
// Reserved uniforms: `texture` (sampler), `u_resolution` (vec2, pixel size of
// the drawable's texture / the chain's RT — both match because the chain feeds
// the source texture's frame). `u_time` is intentionally unused.
//
// Author params:
//   center    : vec2, in pixels of the drawable's texture (matches u_resolution)
//   radius    : float, inner radius (pixels) — fully lit out to here
//   softness  : float, additional radius (pixels) over which light fades to dark
//   inside    : vec3, multiplier inside the light pool (default ~ 1.0)
//   outside   : vec3, multiplier in the shadow (e.g. [0.25, 0.27, 0.40] for cold)
//
// Wire:
//   shader:
//     source: shaders/spotlight.frag
//     params:
//       center:   [640, 360]
//       radius:   180
//       softness: 200
//       inside:   [1.05, 1.02, 0.95]
//       outside:  [0.30, 0.30, 0.45]

uniform sampler2D texture;
uniform vec2  u_resolution;
uniform vec2  center;
uniform float radius;
uniform float softness;
uniform vec3  inside;
uniform vec3  outside;

void main() {
    vec4 px = texture2D(texture, gl_TexCoord[0].xy);
    vec2 fragpx = gl_TexCoord[0].xy * u_resolution;
    float d = distance(fragpx, center);
    // 0 at center→radius, 1 past radius+softness. Smoothstep keeps the ring soft.
    float t = smoothstep(radius, radius + max(softness, 0.001), d);
    vec3 tint = mix(inside, outside, t);
    gl_FragColor = vec4(px.rgb * tint, px.a) * gl_Color;
}
