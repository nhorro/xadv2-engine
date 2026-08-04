#pragma once

#include "engine/gfx/shader_chain.hpp"
#include "engine/pnc/room.hpp"

#include <SFML/Graphics/Rect.hpp>

#include <memory>
#include <vector>

namespace sf {
class Shader;
}

namespace pac::core {
class Diagnostics;
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
                   sf::FloatRect camera_view,
                   float time,
                   pac::core::Diagnostics& log,
                   gfx::RuntimeShaderPass& out) const;

private:
    bool ensure_shader(pac::core::Diagnostics& log) const;

    mutable std::unique_ptr<sf::Shader> shader_;
    mutable bool shader_attempted_ = false;
    mutable bool light_limit_warned_ = false;
};

} // namespace pac::pnc
