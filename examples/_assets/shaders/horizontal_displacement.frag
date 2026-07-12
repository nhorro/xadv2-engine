// Horizontal displacement — wraps the texture horizontally over time. The
// canonical use is a tiling sky layer with clouds: set `speed` (pixels/sec) and
// the sky scrolls forever. Also useful for a flowing river layer painted
// horizontally, or a parallax mid-ground.
//
// Wrapping requires the texture to be authored as a horizontally-tileable image
// (left edge matches right edge). The fract() preserves seamless wrapping.
//
// Reserved uniforms: `texture` (sampler), `u_time` (float), `u_resolution`
// (vec2). `u_resolution.x` is used to convert pixel speed to UV speed.
//
// Author params:
//   speed   : float, pixels/second (positive = scrolls left visually, like
//             clouds moving rightward in the world)
//
// Wire (on a sky layer):
//   shader:
//     source: shaders/horizontal_displacement.frag
//     params: { speed: 18.0 }

uniform sampler2D texture;
uniform float u_time;
uniform vec2  u_resolution;
uniform float speed;

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    // Convert pixel speed to UV-per-second on the U axis.
    uv.x = fract(uv.x + (u_time * speed) / max(u_resolution.x, 1.0));
    vec4 px = texture2D(texture, uv);
    gl_FragColor = px * gl_Color;
}
