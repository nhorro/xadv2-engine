#pragma once

#include "engine/gfx/shader_chain.hpp"
#include "engine/pnc/room.hpp"

#include <SFML/Graphics/Rect.hpp>

#include <memory>
#include <vector>

namespace sf {
class RenderTarget;
class Shader;
class Texture;
}

namespace pac::core {
class Diagnostics;
class ResourceCache;
}

namespace pac::pnc {

struct ResolvedRoomLight {
    const RoomLight* light = nullptr;
    geom::Point position;
    float direction = 0.0f;
    bool enabled = true;
    float intensity = 1.0f;
};

/// Deterministic modulation multiplier for an authored light intensity. Kept
/// pure/headless so flicker and faulty-lamp behavior can be regression-tested.
float evaluate_light_modulation(const LightModulation& modulation, float time);

/// Resolve a dynamic projected shadow at one caster's feet. Each selected light
/// is weighted by its live intensity, luminance, radial attenuation, and spot
/// cone coverage. Multiple sources produce one direction and one silhouette;
/// their total contribution controls opacity. With no contributing source the
/// returned shadow remains present with zero opacity so its contact-shadow policy
/// is still respected.
ProjectedShadow resolve_projected_shadow(const ProjectedShadow& authored,
                                         const std::vector<ResolvedRoomLight>& lights,
                                         geom::Point caster,
                                         float time);

/// Fixed-function fallback used by SFML's Android GLES1 backend. It preserves
/// authored ambient colour and animated omni/spot lights without render
/// textures or GLSL. Occluders and normal maps remain shader-only.
void draw_compat_lighting(sf::RenderTarget& target,
                          const RoomLighting& lighting,
                          const std::vector<ResolvedRoomLight>& resolved,
                          sf::FloatRect camera_view,
                          float time);

/// Applies the portable subset of the standard color_grade.frag parameters
/// (tint, brightness and contrast) with fixed-function blend passes.
void draw_compat_color_grade(sf::RenderTarget& target,
                             const RoomPostProcess* post_process,
                             sf::FloatRect camera_view);

/// Compiles and binds the engine-owned room-lighting shader. The returned pass
/// is inserted before authored post-processing by gfx::ShaderChain, reusing its
/// existing ping-pong render textures.
class RoomLightingRenderer {
public:
    RoomLightingRenderer();
    ~RoomLightingRenderer();

    RoomLightingRenderer(const RoomLightingRenderer&) = delete;
    RoomLightingRenderer& operator=(const RoomLightingRenderer&) = delete;

    bool make_pass(const RoomLighting& lighting,
                   const std::vector<ResolvedRoomLight>& resolved,
                   const std::vector<const LightOccluder*>& occluders,
                   sf::FloatRect camera_view,
                   float time,
                   const std::string& room_dir,
                   pac::core::ResourceCache& resources,
                   pac::core::Diagnostics& log,
                   gfx::RuntimeShaderPass& out) const;

private:
    bool ensure_shader(bool advanced, pac::core::Diagnostics& log) const;

    mutable std::unique_ptr<sf::Shader> shader_;
    mutable std::unique_ptr<sf::Shader> advanced_shader_;
    mutable bool shader_attempted_ = false;
    mutable bool advanced_shader_attempted_ = false;
    mutable bool light_limit_warned_ = false;
    mutable bool occluder_limit_warned_ = false;
    mutable std::string normal_logical_;
    mutable const sf::Texture* normal_texture_ = nullptr;
    mutable bool normal_attempted_ = false;
};

} // namespace pac::pnc
