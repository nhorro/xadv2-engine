#include "pnc/room_lighting.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/gfx/gles2_compat.hpp"

#include <SFML/Config.hpp>
#include <SFML/Graphics/Glsl.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/VertexArray.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <variant>

namespace pac::pnc {

namespace {

constexpr std::size_t kMaxLights = 8;
constexpr std::size_t kMaxOccluderSegments = 32;
constexpr float kPi = 3.14159265358979323846f;

// Common fast path: ambient + omni/spot lights only. Keep this separate from
// the advanced program because some GLSL 1.20 drivers unroll nested occluder
// loops even when their uniform count is zero.
constexpr const char* kLightingFragmentSimple = R"GLSL(
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
    vec2 fragpx = vec2(uv.x * u_resolution.x, (1.0 - uv.y) * u_resolution.y);
    vec3 illumination = ambient_color * ambient_intensity;
    for (int i = 0; i < 8; ++i) {
        if (i >= light_count) break;
        vec4 position_radius = light_position_radius[i];
        vec2 delta = fragpx - position_radius.xy;
        float distance_sq = dot(delta, delta);
        float radius_sq = max(position_radius.z * position_radius.z, 0.0001);
        float attenuation = 1.0 - smoothstep(0.0, 1.0, distance_sq / radius_sq);
        vec4 cone = light_direction_cone[i];
        float cone_factor = 1.0;
        if (cone.z > -1.5) {
            vec2 ray = delta * inversesqrt(max(distance_sq, 0.0001));
            cone_factor = smoothstep(cone.z, cone.w, dot(ray, cone.xy));
        }
        vec4 color_intensity = light_color_intensity[i];
        illumination += color_intensity.rgb * color_intensity.a * attenuation * cone_factor;
    }
    gl_FragColor = vec4(px.rgb * clamp(illumination, 0.0, 1.0), px.a) * gl_Color;
}
)GLSL";

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
uniform int occluder_count;
uniform vec4 occluder_segments[32];
uniform sampler2D normal_texture;
uniform int use_normal_map;
uniform vec2 camera_origin;
uniform vec2 normal_origin;
uniform vec2 normal_size;
uniform float normal_strength;

float cross2(vec2 a, vec2 b) {
    return a.x * b.y - a.y * b.x;
}

bool light_ray_blocked(vec2 light_position, vec2 fragment_position) {
    vec2 ray = fragment_position - light_position;
    for (int j = 0; j < 32; ++j) {
        if (j >= occluder_count) {
            break;
        }
        vec4 segment = occluder_segments[j];
        vec2 edge_start = segment.xy;
        vec2 edge = segment.zw - segment.xy;
        float denominator = cross2(ray, edge);
        if (abs(denominator) < 0.0001) {
            continue;
        }
        vec2 offset = edge_start - light_position;
        float along_ray = cross2(offset, edge) / denominator;
        float along_edge = cross2(offset, ray) / denominator;
        // Exclude the endpoints: a light or shaded pixel seated directly on an
        // authored wall should not self-occlude because of floating-point noise.
        if (along_ray > 0.001 && along_ray < 0.995 &&
            along_edge >= 0.0 && along_edge <= 1.0) {
            return true;
        }
    }
    return false;
}

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
        float visibility = 1.0;
        if (occluder_count > 0 && light_ray_blocked(position_radius.xy, fragpx)) {
            visibility = 0.0;
        }
        float normal_factor = 1.0;
        if (use_normal_map != 0) {
            vec2 normal_uv = (camera_origin + fragpx - normal_origin) / normal_size;
            if (normal_uv.x >= 0.0 && normal_uv.x <= 1.0 &&
                normal_uv.y >= 0.0 && normal_uv.y <= 1.0) {
                vec3 normal = texture2D(normal_texture,
                                        vec2(normal_uv.x, 1.0 - normal_uv.y)).rgb * 2.0 - 1.0;
                normal.xy *= normal_strength;
                normal = normalize(normal);
                vec3 to_light = normalize(vec3(-delta, position_radius.w));
                normal_factor = max(dot(normal, to_light), 0.0);
            }
        }
        illumination += color_intensity.rgb * color_intensity.a * attenuation * cone_factor *
                        visibility * normal_factor;
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

void draw_compat_lighting(sf::RenderTarget& target,
                          const RoomLighting& lighting,
                          const std::vector<ResolvedRoomLight>& resolved,
                          sf::FloatRect camera_view,
                          float time) {
    const auto channel = [](float value) {
        return static_cast<sf::Uint8>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    sf::RectangleShape ambient({camera_view.width, camera_view.height});
    ambient.setPosition(camera_view.left, camera_view.top);
    ambient.setFillColor(sf::Color(channel(lighting.ambient_color[0] *
                                           lighting.ambient_intensity),
                                   channel(lighting.ambient_color[1] *
                                           lighting.ambient_intensity),
                                   channel(lighting.ambient_color[2] *
                                           lighting.ambient_intensity)));
    target.draw(ambient, sf::RenderStates(sf::BlendMultiply));

    const sf::BlendMode add_to_destination(sf::BlendMode::DstColor,
                                           sf::BlendMode::One,
                                           sf::BlendMode::Add);
    constexpr int kSegments = 48;
    constexpr float kDegrees = 3.14159265358979323846f / 180.0f;
    for (const ResolvedRoomLight& item : resolved) {
        if (!item.light || !item.enabled || item.intensity <= 0.0f) {
            continue;
        }
        const RoomLight& light = *item.light;
        const float intensity = std::clamp(
            item.intensity * evaluate_light_modulation(light.modulation, time), 0.0f, 1.0f);
        const sf::Color center(channel(light.color[0] * intensity),
                               channel(light.color[1] * intensity),
                               channel(light.color[2] * intensity));
        sf::VertexArray fan(sf::TriangleFan);
        fan.append(sf::Vertex({item.position.x, item.position.y}, center));
        const bool spot = light.type == RoomLight::Type::SPOT;
        const float start = spot ? item.direction - light.angle * 0.5f : 0.0f;
        const float sweep = spot ? light.angle : 360.0f;
        const int segments = spot ? std::max(8, static_cast<int>(std::ceil(kSegments * sweep / 360.0f)))
                                  : kSegments;
        for (int i = 0; i <= segments; ++i) {
            const float angle = (start + sweep * static_cast<float>(i) /
                                            static_cast<float>(segments)) * kDegrees;
            fan.append(sf::Vertex({item.position.x + std::cos(angle) * light.radius,
                                   item.position.y + std::sin(angle) * light.radius},
                                  sf::Color::Black));
        }
        sf::RenderStates states;
        states.blendMode = add_to_destination;
        target.draw(fan, states);
    }
}

void draw_compat_color_grade(sf::RenderTarget& target,
                             const RoomPostProcess* post_process,
                             sf::FloatRect camera_view) {
    if (!post_process || !post_process->enabled) {
        return;
    }
    const auto scalar = [](const gfx::ShaderEffect& effect,
                           const std::string& name,
                           float fallback) {
        for (const gfx::ShaderParam& param : effect.params) {
            if (param.name == name) {
                if (const auto* value = std::get_if<float>(&param.value)) return *value;
            }
        }
        return fallback;
    };
    const auto vector3 = [](const gfx::ShaderEffect& effect,
                            const std::string& name,
                            std::array<float, 3> fallback) {
        for (const gfx::ShaderParam& param : effect.params) {
            if (param.name == name) {
                if (const auto* value = std::get_if<std::array<float, 3>>(&param.value)) return *value;
            }
        }
        return fallback;
    };
    const auto channel = [](float value) {
        return static_cast<sf::Uint8>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    for (const gfx::ShaderEffect& effect : post_process->shaders) {
        if (!effect.enabled || !effect.controller.empty() ||
            !effect.source.ends_with("color_grade.frag")) {
            continue;
        }
        const float strength = std::clamp(scalar(effect, "strength", 1.0f), 0.0f, 1.0f);
        const auto tint = vector3(effect, "tint", {1.0f, 1.0f, 1.0f});
        const float contrast = 1.0f + (scalar(effect, "contrast", 1.0f) - 1.0f) * strength;
        const float brightness = scalar(effect, "brightness", 0.0f) * strength;
        sf::RectangleShape pass({camera_view.width, camera_view.height});
        pass.setPosition(camera_view.left, camera_view.top);
        pass.setFillColor(sf::Color(channel((1.0f + (tint[0] - 1.0f) * strength) *
                                            std::min(contrast, 1.0f)),
                                    channel((1.0f + (tint[1] - 1.0f) * strength) *
                                            std::min(contrast, 1.0f)),
                                    channel((1.0f + (tint[2] - 1.0f) * strength) *
                                            std::min(contrast, 1.0f))));
        target.draw(pass, sf::RenderStates(sf::BlendMultiply));
        if (contrast > 1.0f) {
            pass.setFillColor(sf::Color(channel(contrast - 1.0f),
                                        channel(contrast - 1.0f),
                                        channel(contrast - 1.0f)));
            sf::RenderStates states;
            states.blendMode = sf::BlendMode(sf::BlendMode::DstColor,
                                             sf::BlendMode::One,
                                             sf::BlendMode::Add);
            target.draw(pass, states);
        }
        if (brightness != 0.0f) {
            if (brightness > 0.0f) {
                pass.setFillColor(sf::Color(255, 255, 255, channel(brightness)));
                target.draw(pass, sf::RenderStates(sf::BlendAlpha));
            } else {
                const float multiplier = std::clamp(1.0f + brightness, 0.0f, 1.0f);
                pass.setFillColor(sf::Color(channel(multiplier), channel(multiplier),
                                            channel(multiplier)));
                target.draw(pass, sf::RenderStates(sf::BlendMultiply));
            }
        }
    }
}

RoomLightingRenderer::RoomLightingRenderer() = default;
RoomLightingRenderer::~RoomLightingRenderer() = default;

bool RoomLightingRenderer::ensure_shader(bool advanced, pac::core::Diagnostics& log) const {
    bool& attempted = advanced ? advanced_shader_attempted_ : shader_attempted_;
    std::unique_ptr<sf::Shader>& program = advanced ? advanced_shader_ : shader_;
    if (attempted) {
        return static_cast<bool>(program);
    }
    attempted = true;
    if (!sf::Shader::isAvailable()) {
        log.error(
            "room lighting: shaders are unavailable on this GPU; dynamic lights are disabled");
        return false;
    }
    auto shader = std::make_unique<sf::Shader>();
    const char* source = advanced ? kLightingFragment : kLightingFragmentSimple;
#if defined(SFML_SYSTEM_ANDROID)
    const std::string es_source = pac::gfx::make_gles2_fragment_shader(source);
    const bool loaded =
        shader->loadFromMemory(pac::gfx::gles2_vertex_shader_source(), es_source);
#else
    const bool loaded = shader->loadFromMemory(source, sf::Shader::Fragment);
#endif
    if (!loaded) {
        log.error("room lighting: built-in lighting shader failed to compile");
        return false;
    }
    program = std::move(shader);
    return true;
}

bool RoomLightingRenderer::make_pass(const RoomLighting& lighting,
                                     const std::vector<ResolvedRoomLight>& resolved,
                                     const std::vector<const LightOccluder*>& occluders,
                                     sf::FloatRect camera_view,
                                     float time,
                                     const std::string& room_dir,
                                     pac::core::ResourceCache& resources,
                                     pac::core::Diagnostics& log,
                                     gfx::RuntimeShaderPass& out) const {
    const bool advanced = !occluders.empty() || !lighting.normal_map.empty();
    if (!ensure_shader(advanced, log)) {
        return false;
    }

    std::array<sf::Glsl::Vec4, kMaxLights> positions{};
    std::array<sf::Glsl::Vec4, kMaxLights> colors{};
    std::array<sf::Glsl::Vec4, kMaxLights> cones{};
    std::array<sf::Glsl::Vec4, kMaxOccluderSegments> segments{};
    std::size_t count = 0;
    std::size_t visible_count = 0;
    for (const ResolvedRoomLight& resolved_light : resolved) {
        if (!resolved_light.light || !resolved_light.enabled || resolved_light.intensity <= 0.0f) {
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

        positions[count] = sf::Glsl::Vec4(x, y, light.radius, light.height);
        const float intensity =
            std::clamp(resolved_light.intensity * evaluate_light_modulation(light.modulation, time),
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

    std::size_t segment_count = 0;
    std::size_t authored_segment_count = 0;
    for (const LightOccluder* occluder : occluders) {
        if (!occluder || occluder->area.size() < 2) {
            continue;
        }
        const std::size_t edge_count = occluder->area.size() == 2 ? 1 : occluder->area.size();
        authored_segment_count += edge_count;
        for (std::size_t i = 0; i < edge_count && segment_count < kMaxOccluderSegments; ++i) {
            const geom::Point& a = occluder->area[i];
            const geom::Point& b = occluder->area[(i + 1) % occluder->area.size()];
            segments[segment_count++] = sf::Glsl::Vec4(a.x - camera_view.left,
                                                       a.y - camera_view.top,
                                                       b.x - camera_view.left,
                                                       b.y - camera_view.top);
        }
    }
    if (authored_segment_count > kMaxOccluderSegments && !occluder_limit_warned_) {
        log.warn("room lighting: more than 32 occluder edges are authored; only the first 32 "
                 "are rendered");
        occluder_limit_warned_ = true;
    }

    const sf::Texture* normal_texture = nullptr;
    if (!lighting.normal_map.empty()) {
        const std::string logical = pac::core::logical_join(room_dir, lighting.normal_map);
        if (logical != normal_logical_) {
            normal_logical_ = logical;
            normal_texture_ = nullptr;
            normal_attempted_ = false;
        }
        if (!normal_attempted_) {
            normal_attempted_ = true;
            try {
                normal_texture_ = &resources.texture(logical);
            } catch (const std::exception& e) {
                log.error(std::string("room lighting: ") + e.what());
            }
        }
        normal_texture = normal_texture_;
    }

    const sf::Glsl::Vec3 ambient(lighting.ambient_color[0],
                                 lighting.ambient_color[1],
                                 lighting.ambient_color[2]);
    const sf::Glsl::Vec2 resolution(camera_view.width, camera_view.height);
    const sf::Glsl::Vec2 camera_origin(camera_view.left, camera_view.top);
    const sf::Glsl::Vec2 normal_origin(lighting.normal_origin.x, lighting.normal_origin.y);
    const sf::Vector2u normal_pixels =
        normal_texture ? normal_texture->getSize() : sf::Vector2u(1, 1);
    const sf::Glsl::Vec2 normal_size(static_cast<float>(normal_pixels.x) * lighting.normal_scale,
                                     static_cast<float>(normal_pixels.y) * lighting.normal_scale);
    out.shader = advanced ? advanced_shader_.get() : shader_.get();
    out.bind = [ambient,
                resolution,
                intensity = lighting.ambient_intensity,
                positions,
                colors,
                cones,
                segments,
                segment_count,
                camera_origin,
                normal_origin,
                normal_size,
                normal_strength = lighting.normal_strength,
                normal_texture,
                advanced,
                count](sf::Shader& shader) {
        shader.setUniform("texture", sf::Shader::CurrentTexture);
        shader.setUniform("u_resolution", resolution);
        shader.setUniform("ambient_color", ambient);
        shader.setUniform("ambient_intensity", intensity);
        shader.setUniform("light_count", static_cast<int>(count));
        if (advanced) {
            shader.setUniform("occluder_count", static_cast<int>(segment_count));
            shader.setUniform("use_normal_map", normal_texture ? 1 : 0);
            shader.setUniform("camera_origin", camera_origin);
            shader.setUniform("normal_origin", normal_origin);
            shader.setUniform("normal_size", normal_size);
            shader.setUniform("normal_strength", normal_strength);
            if (normal_texture) {
                shader.setUniform("normal_texture", *normal_texture);
            }
        }
        if (count > 0) {
            shader.setUniformArray("light_position_radius", positions.data(), count);
            shader.setUniformArray("light_color_intensity", colors.data(), count);
            shader.setUniformArray("light_direction_cone", cones.data(), count);
        }
        if (advanced && segment_count > 0) {
            shader.setUniformArray("occluder_segments", segments.data(), segment_count);
        }
    };
    return true;
}

} // namespace pac::pnc
