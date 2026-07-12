// Color grade — brightness / contrast / saturation / tint, the everyday mood
// knob. Complements `ambient_grade.frag` (which is tint-multiply only); this one
// is the full instrument. Pure params, no `u_time` / `u_resolution`.
//
// Author params:
//   tint        : vec3, per-channel multiplier   (default ~ 1.0)
//   brightness  : float, additive               (-1..1, 0 = none)
//   contrast    : float, around 0.5 midgray     (1 = none, 1.2 = punchier)
//   saturation  : float, 1 = original, 0 = grey (luma weights Rec.601)
//   strength    : float, mix to original        (0 = original, 1 = full grade)
//
// Wire:
//   shader:
//     source: shaders/color_grade.frag
//     params:
//       tint:       [1.05, 1.0, 0.92]
//       brightness: -0.03
//       contrast:   1.10
//       saturation: 0.90
//       strength:   0.6

uniform sampler2D texture;
uniform vec3  tint;
uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform float strength;

void main() {
    vec4 px = texture2D(texture, gl_TexCoord[0].xy);
    vec3 c = px.rgb * tint + vec3(brightness);
    c = (c - 0.5) * contrast + 0.5;
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(luma), c, saturation);
    gl_FragColor = vec4(mix(px.rgb, c, strength), px.a) * gl_Color;
}
