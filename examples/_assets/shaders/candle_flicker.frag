// Candle / failing-bulb flicker — the worked example for the reserved `u_time`
// uniform (design 03 §Shaders). The engine sets `u_time` (seconds) every frame;
// here it drives an irregular brightness wobble (stepped value-noise, not an
// obvious sine, per the art direction notes in issue #56).
//
// Reference asset for the future shader showcase; wire it onto a layer/region
// with:  shader: { source: shaders/candle_flicker.frag,
//                  params: { intensity: 0.06, speed: 7.0 } }

uniform sampler2D texture; // engine-bound: the drawable's texture
uniform float u_time;      // engine-bound: seconds since the scene began
uniform float intensity;   // wobble amount, e.g. 0.06
uniform float speed;       // wobble speed, e.g. 7.0

float noise(float x) {
    return fract(sin(x * 12.9898) * 43758.5453);
}

void main() {
    vec4 px = texture2D(texture, gl_TexCoord[0].xy);
    float t = u_time * speed;
    float flick = mix(noise(floor(t)), noise(floor(t) + 1.0), fract(t));
    float b = 1.0 + (flick - 0.5) * 2.0 * intensity;
    gl_FragColor = vec4(px.rgb * b, px.a) * gl_Color;
}
