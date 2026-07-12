// Vignette — soft darkening towards the edges (and corners), the cheap mood
// shot. Sample wiring stacks this with `ambient_grade.frag` on the study layer,
// exercising the multi-pass path from issue #105.
//
// Reserved uniforms: `texture` (sampler). The effect is computed in normalized
// texture coordinates so it tracks the drawable regardless of size, so
// `u_resolution` is intentionally not used.
//
// Author params:
//   strength : float, how dark the corners go (0 = off, 1 = black at corners)
//   inner    : float, radius (in normalized half-diagonal units) of the fully
//              unaffected center (default 0.45)
//   outer    : float, radius where the falloff reaches `strength` (default 0.95)
//   color    : vec3, the vignette colour to mix toward (default black). A warm
//              [0.15, 0.05, 0.0] gives a sepia/oil-lamp feel.
//
// Wire:
//   shader:
//     source: shaders/vignette.frag
//     params: { strength: 0.55, inner: 0.45, outer: 0.95 }

uniform sampler2D texture;
uniform float strength;
uniform float inner;
uniform float outer;
uniform vec3  color;

void main() {
    vec4 px = texture2D(texture, gl_TexCoord[0].xy);
    // Distance from the texture center, normalized to [0, ~1] at the corner.
    vec2 d = gl_TexCoord[0].xy - vec2(0.5);
    float r = length(d) * 1.41421356; // /max distance to corner from center
    float t = smoothstep(inner, max(outer, inner + 0.001), r);
    vec3 c = mix(px.rgb, color, t * strength);
    gl_FragColor = vec4(c, px.a) * gl_Color;
}
