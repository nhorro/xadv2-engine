#include "pnc/room_lighting.hpp"

#include "engine/core/diagnostics.hpp"

#include <SFML/Graphics/Glsl.hpp>
#include <SFML/Graphics/Shader.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace pac::pnc {

namespace {

constexpr std::size_t kMaxLights = 8;
constexpr float kPi = 3.14159265358979323846f;

// GLSL 1.20-compatible: SFML 2.6 creates compatibility contexts on the engine's
// supported targets. A fixed loop bound avoids driver-dependent dynamic loops;
// light_count lets inactive array slots exit cheaply.
constexpr const char* kLightingFragment = R"GLSL(
uniform sampler2D texture;
uniform vec2 u_resolution;
uniform vec3 ambient_color;
uniform float ambient_intensity;
uniform int light_count;
uniform vec4 light_position_radius[8];
uniform vec4 light_color_intensity[8];
uniform vec4 light_direction_cone[8];

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec4 px = texture2D(texture, uv);
    // sf::RenderTexture is sampled upside-down by OpenGL even though SFML's
    // sprite texture matrix presents the image correctly. World/camera light
    // positions use top-left screen coordinates, so flip only the coordinate
    // used for lighting math—not the texture sample itself.
    vec2 fragpx = vec2(uv.x * u_resolution.x, (1.0 - uv.y) * u_resolution.y);
    vec3 illumination = ambient_color * ambient_intensity;

    for (int i = 0; i < 8; ++i) {
        if (i >= light_count) {
            break;
        }
        vec4 position_radius = light_position_radius[i];
        vec2 delta = fragpx - position_radius.xy;
        float distance_sq = dot(delta, delta);
        float radius_sq = max(position_radius.z * position_radius.z, 0.0001);
        float attenuation = 1.0 - smoothstep(0.0, 1.0, distance_sq / radius_sq);

        vec4 cone = light_direction_cone[i];
        float cone_factor = 1.0;
        if (cone.z > -1.5) {
            vec2 ray = delta * inversesqrt(max(distance_sq, 0.0001));
            float alignment = dot(ray, cone.xy);
            cone_factor = smoothstep(cone.z, cone.w, alignment);
        }

        vec4 color_intensity = light_color_intensity[i];
        illumination += color_intensity.rgb * color_intensity.a * attenuation * cone_factor;
    }

    // This is an LDR painterly pipeline: a light restores/tints the neutral art
    // from ambient darkness but does not create HDR values that would clamp in
    // the RGBA8 ping-pong target before the grading pass.
    vec3 lit = px.rgb * clamp(illumination, 0.0, 1.0);
    gl_FragColor = vec4(lit, px.a) * gl_Color;
}
)GLSL";

float hash(float value) {
    const float raw = std::sin(value * 12.9898f) * 43758.5453f;
    return raw - std::floor(raw);
}

float value_noise(float value) {
    const float cell = std::floor(value);
    const float fraction = value - cell;
    const float smooth = fraction * fraction * (3.0f - 2.0f * fraction);
    const float a = hash(cell);
    const float b = hash(cell + 1.0f);
    return a + (b - a) * smooth;
}

} // namespace

float evaluate_light_modulation(const LightModulation& modulation, float time) {
    const float amount = std::clamp(modulation.amount, 0.0f, 1.0f);
    if (modulation.type == LightModulation::Type::NONE || amount <= 0.0f) {
        return 1.0f;
    }

    const float phase = time * std::max(modulation.speed, 0.001f) + modulation.seed;
    float scale = 1.0f;
    switch (modulation.type) {
    case LightModulation::Type::NONE:
        break;
    case LightModulation::Type::SINE:
        scale += std::sin(phase * 2.0f * kPi) * amount;
        break;
    case LightModulation::Type::FLICKER: {
        const float slow = value_noise(phase);
        const float fast = value_noise(phase * 2.37f + modulation.seed * 19.0f);
        const float noise = slow * 0.72f + fast * 0.28f;
        scale += (noise * 2.0f - 1.0f) * amount;
        break;
    }
    case LightModulation::Type::FAULTY: {
        const float cell = std::floor(phase);
        const float cell_noise = hash(cell + modulation.seed * 31.0f);
        const float flutter = value_noise(phase * 3.1f + modulation.seed * 7.0f);
        scale += (flutter * 2.0f - 1.0f) * amount * 0.22f;
        if (cell_noise < 0.18f) {
            scale *= 1.0f - amount * (0.75f + hash(cell + 91.0f) * 0.25f);
        }
        break;
    }
    }
    return std::max(0.0f, scale);
}

RoomLightingRenderer::RoomLightingRenderer() = default;
RoomLightingRenderer::~RoomLightingRenderer() = default;

bool RoomLightingRenderer::ensure_shader(pac::core::Diagnostics& log) const {
    if (shader_attempted_) {
        return static_cast<bool>(shader_);
    }
    shader_attempted_ = true;
    if (!sf::Shader::isAvailable()) {
        log.error(
            "room lighting: shaders are unavailable on this GPU; dynamic lights are disabled");
        return false;
    }
    auto shader = std::make_unique<sf::Shader>();
    if (!shader->loadFromMemory(kLightingFragment, sf::Shader::Fragment)) {
        log.error("room lighting: built-in lighting shader failed to compile");
        return false;
    }
    shader_ = std::move(shader);
    return true;
}

bool RoomLightingRenderer::make_pass(const RoomLighting& lighting,
                                     const std::vector<ResolvedRoomLight>& resolved,
                                     sf::FloatRect camera_view,
                                     float time,
                                     pac::core::Diagnostics& log,
                                     gfx::RuntimeShaderPass& out) const {
    if (!ensure_shader(log)) {
        return false;
    }

    std::array<sf::Glsl::Vec4, kMaxLights> positions{};
    std::array<sf::Glsl::Vec4, kMaxLights> colors{};
    std::array<sf::Glsl::Vec4, kMaxLights> cones{};
    std::size_t count = 0;
    std::size_t visible_count = 0;
    for (const ResolvedRoomLight& resolved_light : resolved) {
        if (!resolved_light.light || !resolved_light.light->enabled ||
            resolved_light.light->intensity <= 0.0f) {
            continue;
        }
        const RoomLight& light = *resolved_light.light;
        const float x = resolved_light.position.x - camera_view.left;
        const float y = resolved_light.position.y - camera_view.top;
        if (x + light.radius < 0.0f || y + light.radius < 0.0f ||
            x - light.radius > camera_view.width || y - light.radius > camera_view.height) {
            continue;
        }
        ++visible_count;
        if (count >= kMaxLights) {
            continue;
        }

        positions[count] = sf::Glsl::Vec4(x, y, light.radius, 0.0f);
        const float intensity =
            std::clamp(light.intensity * evaluate_light_modulation(light.modulation, time),
                       0.0f,
                       4.0f);
        colors[count] = sf::Glsl::Vec4(light.color[0], light.color[1], light.color[2], intensity);

        if (light.type == RoomLight::Type::SPOT) {
            const float direction_radians = resolved_light.direction * kPi / 180.0f;
            const float outer = light.angle * 0.5f * kPi / 180.0f;
            const float inner = (light.angle * 0.5f - light.softness) * kPi / 180.0f;
            cones[count] = sf::Glsl::Vec4(std::cos(direction_radians),
                                          std::sin(direction_radians),
                                          std::cos(outer),
                                          std::cos(inner));
        } else {
            cones[count] = sf::Glsl::Vec4(0.0f, 0.0f, -2.0f, -2.0f);
        }
        ++count;
    }

    if (visible_count > kMaxLights && !light_limit_warned_) {
        log.warn(
            "room lighting: more than 8 lights overlap the camera; only the first 8 are rendered");
        light_limit_warned_ = true;
    }

    const sf::Glsl::Vec3 ambient(lighting.ambient_color[0],
                                 lighting.ambient_color[1],
                                 lighting.ambient_color[2]);
    const sf::Glsl::Vec2 resolution(camera_view.width, camera_view.height);
    out.shader = shader_.get();
    out.bind = [ambient,
                resolution,
                intensity = lighting.ambient_intensity,
                positions,
                colors,
                cones,
                count](sf::Shader& shader) {
        shader.setUniform("texture", sf::Shader::CurrentTexture);
        shader.setUniform("u_resolution", resolution);
        shader.setUniform("ambient_color", ambient);
        shader.setUniform("ambient_intensity", intensity);
        shader.setUniform("light_count", static_cast<int>(count));
        if (count > 0) {
            shader.setUniformArray("light_position_radius", positions.data(), count);
            shader.setUniformArray("light_color_intensity", colors.data(), count);
            shader.setUniformArray("light_direction_cone", cones.data(), count);
        }
    };
    return true;
}

} // namespace pac::pnc
