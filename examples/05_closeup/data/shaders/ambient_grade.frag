// Ambient colour grade — the worked example for the engine's declarative shader
// path (design 03 §Shaders). Pure params, no time: multiplies the layer colour by
// `tint` and blends it in by `strength`. A cheap way to shift a room's mood
// (warm lamp light, cold night) without repainting the art.
//
// Reserved uniform `texture` is the layer's image, bound by the engine.
// Author params (from the room YAML) follow.

uniform sampler2D texture; // engine-bound: the drawable's texture
uniform vec3 tint;         // per-channel multiplier (e.g. warm = [1.05, 1.0, 0.92])
uniform float strength;    // 0 = original, 1 = fully graded

void main() {
    vec4 px = texture2D(texture, gl_TexCoord[0].xy);
    vec3 graded = px.rgb * tint;
    gl_FragColor = vec4(mix(px.rgb, graded, strength), px.a) * gl_Color;
}
