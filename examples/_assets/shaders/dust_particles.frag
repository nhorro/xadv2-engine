// Particle dust — animated floating motes overlaid on the drawable. Cheap
// effect: a grid of pseudo-random sparkles, jittered over time, summed as a
// brightness add. Use for a sunlit room ("dust in the lamplight"), an attic, an
// archive — atmosphere shot without an extra spritesheet.
//
// Reserved uniforms: `texture` (sampler), `u_time` (float), `u_resolution`
// (vec2). The grid is computed in texture-pixel space so the density stays
// constant regardless of the drawable's size.
//
// Author params:
//   density   : float, motes per 100 pixels (higher = denser, e.g. 0.6)
//   color     : vec3, mote colour (default warm white)
//   intensity : float, peak brightness added per mote (e.g. 0.35)
//   speed     : float, downward drift in pixels/sec (e.g. 12)
//   size      : float, mote radius in pixels (e.g. 1.6)
//
// Wire:
//   shader:
//     source: shaders/dust_particles.frag
//     params: { density: 0.6, color: [1.0, 0.96, 0.85], intensity: 0.4, speed: 10, size: 1.6 }

uniform sampler2D texture;
uniform float u_time;
uniform vec2  u_resolution;
uniform float density;
uniform vec3  color;
uniform float intensity;
uniform float speed;
uniform float size;

// 2D hash → [0,1). Stable per cell, cheap.
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec4 px = texture2D(texture, gl_TexCoord[0].xy);
    // Cell size: a denser grid → more potential motes. Density is normalized so
    // an author tuning around 0.5..1.0 makes sense.
    float cell = max(20.0 - density * 18.0, 4.0);
    vec2 fragpx = gl_TexCoord[0].xy * u_resolution;
    // Drift the world downward over time, so motes appear to fall (or rise — try
    // a negative speed).
    vec2 worldpx = fragpx + vec2(0.0, u_time * speed);
    vec2 cellpos = floor(worldpx / cell);
    vec2 local   = worldpx - cellpos * cell;
    // One mote per cell at a random position; brightness gated by a chance.
    vec2 motepos = vec2(hash21(cellpos), hash21(cellpos + 17.0)) * cell;
    float chance = step(0.5, hash21(cellpos + 91.0));
    float d = distance(local, motepos);
    float k = (1.0 - smoothstep(0.0, max(size, 0.001), d)) * chance * intensity;
    vec3 c = px.rgb + color * k;
    gl_FragColor = vec4(c, px.a) * gl_Color;
}
