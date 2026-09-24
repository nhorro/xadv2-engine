#pragma once

#include "engine/pnc/room.hpp"

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sf {
class RenderTexture;
class Texture;
} // namespace sf

namespace pac::pnc {

struct RoomSmokeParticle {
    geom::Point position;
    float age = 0.0f;
    float lifetime = 1.0f;
    float phase = 0.0f;
    std::size_t emitter = 0;
};

/// CPU-pooled smoke simulation plus its single low-resolution density draw.
/// No particle owns a texture or drawable; all visual state comes from its
/// emitter and one shared radial texture.
class RoomSmokeSystem {
public:
    RoomSmokeSystem();
    ~RoomSmokeSystem();

    RoomSmokeSystem(const RoomSmokeSystem&) = delete;
    RoomSmokeSystem& operator=(const RoomSmokeSystem&) = delete;

    void configure(const RoomSmoke& smoke);
    void reset();
    void update(float dt);

    [[nodiscard]] bool active() const;
    [[nodiscard]] std::size_t particle_count() const { return particles_.size(); }
    [[nodiscard]] const std::vector<RoomSmokeParticle>& particles() const { return particles_; }

    /// Rebuild and return the current camera-space density field. The returned
    /// texture remains owned by this system and valid until the next call.
    const sf::Texture* render_density(sf::FloatRect camera_view, sf::Vector2u viewport) const;

private:
    struct EmitterState {
        float emission_accumulator = 0.0f;
        std::size_t live_particles = 0;
        std::uint32_t random_state = 1;
    };

    float random01(std::size_t emitter);
    geom::Point random_point(std::size_t emitter);
    bool spawn(std::size_t emitter, float initial_age = 0.0f);
    bool ensure_graphics() const;

    RoomSmoke config_;
    bool configured_ = false;
    std::vector<EmitterState> emitters_;
    std::vector<RoomSmokeParticle> particles_;
    mutable std::unique_ptr<sf::Texture> particle_texture_;
    mutable std::unique_ptr<sf::RenderTexture> density_target_;
    mutable std::size_t density_rt_bytes_ = 0;
};

} // namespace pac::pnc
