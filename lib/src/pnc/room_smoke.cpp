#include "pnc/room_smoke.hpp"

#include "engine/core/render_stats.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <SFML/Graphics/View.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pac::pnc {

namespace {

constexpr unsigned kParticleTextureSize = 64;
constexpr float kPi = 3.14159265358979323846f;

float particle_envelope(float normalized_age) {
    const float fade_in = std::clamp(normalized_age / 0.16f, 0.0f, 1.0f);
    const float fade_out = std::clamp((1.0f - normalized_age) / 0.28f, 0.0f, 1.0f);
    return fade_in * fade_out;
}

} // namespace

RoomSmokeSystem::RoomSmokeSystem() = default;

RoomSmokeSystem::~RoomSmokeSystem() {
    if (density_rt_bytes_ != 0) {
        pac::core::add_shader_rt_bytes(-static_cast<std::ptrdiff_t>(density_rt_bytes_));
    }
}

void RoomSmokeSystem::configure(const RoomSmoke& smoke) {
    config_ = smoke;
    configured_ = true;
    particles_.clear();
    emitters_.clear();
    emitters_.resize(config_.emitters.size());

    std::size_t capacity = 0;
    for (std::size_t i = 0; i < config_.emitters.size(); ++i) {
        const RoomSmokeEmitter& emitter = config_.emitters[i];
        capacity += emitter.max_particles;
        emitters_[i].random_state = emitter.seed == 0 ? 1u : emitter.seed;
    }
    particles_.reserve(capacity);

    if (!config_.enabled) {
        return;
    }
    for (std::size_t i = 0; i < config_.emitters.size(); ++i) {
        const RoomSmokeEmitter& emitter = config_.emitters[i];
        if (!emitter.prewarm) {
            continue;
        }
        const float average_lifetime = (emitter.lifetime_min + emitter.lifetime_max) * 0.5f;
        const std::size_t count =
            std::min(emitter.max_particles,
                     static_cast<std::size_t>(std::ceil(emitter.emission_rate * average_lifetime)));
        for (std::size_t n = 0; n < count; ++n) {
            const float age = random01(i) * average_lifetime;
            (void) spawn(i, age);
        }
    }
}

void RoomSmokeSystem::reset() {
    particles_.clear();
    emitters_.clear();
    config_ = {};
    configured_ = false;
}

bool RoomSmokeSystem::active() const {
    return configured_ && config_.enabled && !config_.emitters.empty();
}

float RoomSmokeSystem::random01(std::size_t emitter) {
    std::uint32_t& value = emitters_[emitter].random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

geom::Point RoomSmokeSystem::random_point(std::size_t emitter_index) {
    const geom::Polygon& area = config_.emitters[emitter_index].area;
    if (area.size() == 1) {
        return area.front();
    }
    const sf::FloatRect bounds = geom::polygon_bounds(area);
    geom::Point candidate{bounds.left, bounds.top};
    for (int attempt = 0; attempt < 24; ++attempt) {
        candidate = {bounds.left + random01(emitter_index) * bounds.width,
                     bounds.top + random01(emitter_index) * bounds.height};
        if (geom::point_in_polygon(candidate, area)) {
            return candidate;
        }
    }
    return area.front();
}

bool RoomSmokeSystem::spawn(std::size_t emitter_index, float initial_age) {
    if (emitter_index >= config_.emitters.size()) {
        return false;
    }
    const RoomSmokeEmitter& emitter = config_.emitters[emitter_index];
    EmitterState& state = emitters_[emitter_index];
    if (state.live_particles >= emitter.max_particles ||
        particles_.size() >= particles_.capacity()) {
        return false;
    }

    RoomSmokeParticle particle;
    particle.emitter = emitter_index;
    particle.position = random_point(emitter_index);
    particle.lifetime = emitter.lifetime_min +
                        random01(emitter_index) * (emitter.lifetime_max - emitter.lifetime_min);
    particle.age = std::min(initial_age, particle.lifetime * 0.98f);
    particle.phase = random01(emitter_index) * 2.0f * kPi;
    particle.position.x += emitter.velocity.x * particle.age;
    particle.position.y += emitter.velocity.y * particle.age;
    particles_.push_back(particle);
    ++state.live_particles;
    return true;
}

void RoomSmokeSystem::update(float dt) {
    if (!active() || !(dt > 0.0f) || !std::isfinite(dt)) {
        return;
    }
    // Avoid a debugger pause turning into a one-frame emission storm.
    dt = std::min(dt, 0.25f);

    for (std::size_t i = 0; i < particles_.size();) {
        RoomSmokeParticle& particle = particles_[i];
        const RoomSmokeEmitter& emitter = config_.emitters[particle.emitter];
        particle.age += dt;
        if (particle.age >= particle.lifetime) {
            --emitters_[particle.emitter].live_particles;
            particle = particles_.back();
            particles_.pop_back();
            continue;
        }
        const float sway = std::sin(particle.age * 0.73f + particle.phase) * emitter.turbulence;
        const float lift =
            std::cos(particle.age * 0.41f + particle.phase) * emitter.turbulence * 0.2f;
        particle.position.x += (emitter.velocity.x + sway) * dt;
        particle.position.y += (emitter.velocity.y + lift) * dt;
        ++i;
    }

    for (std::size_t i = 0; i < config_.emitters.size(); ++i) {
        const RoomSmokeEmitter& emitter = config_.emitters[i];
        EmitterState& state = emitters_[i];
        state.emission_accumulator += emitter.emission_rate * dt;
        while (state.emission_accumulator >= 1.0f) {
            if (!spawn(i)) {
                state.emission_accumulator = std::min(state.emission_accumulator, 1.0f);
                break;
            }
            state.emission_accumulator -= 1.0f;
        }
    }
}

bool RoomSmokeSystem::ensure_graphics() const {
    if (!particle_texture_) {
        sf::Image image;
        image.create(kParticleTextureSize, kParticleTextureSize, sf::Color::Transparent);
        const float center = (static_cast<float>(kParticleTextureSize) - 1.0f) * 0.5f;
        for (unsigned y = 0; y < kParticleTextureSize; ++y) {
            for (unsigned x = 0; x < kParticleTextureSize; ++x) {
                const float dx = (static_cast<float>(x) - center) / center;
                const float dy = (static_cast<float>(y) - center) / center;
                const float distance = std::sqrt(dx * dx + dy * dy);
                const float alpha = std::pow(std::clamp(1.0f - distance, 0.0f, 1.0f), 2.2f);
                image.setPixel(
                    x,
                    y,
                    sf::Color(255, 255, 255, static_cast<sf::Uint8>(std::lround(alpha * 255.0f))));
            }
        }
        auto texture = std::make_unique<sf::Texture>();
        if (!texture->loadFromImage(image)) {
            return false;
        }
        texture->setSmooth(true);
        particle_texture_ = std::move(texture);
    }
    return true;
}

const sf::Texture* RoomSmokeSystem::render_density(sf::FloatRect camera_view,
                                                   sf::Vector2u viewport) const {
    if (!active() || viewport.x == 0 || viewport.y == 0 || !ensure_graphics()) {
        return nullptr;
    }

    const sf::Vector2u density_size{
        std::max(1u, static_cast<unsigned>(std::ceil(viewport.x * config_.resolution_scale))),
        std::max(1u, static_cast<unsigned>(std::ceil(viewport.y * config_.resolution_scale)))};
    if (!density_target_ || density_target_->getSize() != density_size) {
        auto target = std::make_unique<sf::RenderTexture>();
        if (!target->create(density_size.x, density_size.y)) {
            return nullptr;
        }
        target->setSmooth(true);
        density_target_ = std::move(target);
        const std::size_t bytes = static_cast<std::size_t>(density_size.x) * density_size.y * 4;
        pac::core::add_shader_rt_bytes(static_cast<std::ptrdiff_t>(bytes) -
                                       static_cast<std::ptrdiff_t>(density_rt_bytes_));
        density_rt_bytes_ = bytes;
    }

    sf::VertexArray vertices(sf::Quads, particles_.size() * 4);
    std::size_t vertex = 0;
    for (const RoomSmokeParticle& particle : particles_) {
        const RoomSmokeEmitter& emitter = config_.emitters[particle.emitter];
        const float age = particle.age / particle.lifetime;
        const float size = emitter.size_start + (emitter.size_end - emitter.size_start) * age;
        const float half = size * 0.5f;
        const float alpha = std::clamp(emitter.density * particle_envelope(age), 0.0f, 1.0f);
        const sf::Color color(255, 255, 255, static_cast<sf::Uint8>(std::lround(alpha * 255.0f)));
        const float texture_max = static_cast<float>(kParticleTextureSize);
        vertices[vertex++] = sf::Vertex({particle.position.x - half, particle.position.y - half},
                                        color,
                                        {0.0f, 0.0f});
        vertices[vertex++] = sf::Vertex({particle.position.x + half, particle.position.y - half},
                                        color,
                                        {texture_max, 0.0f});
        vertices[vertex++] = sf::Vertex({particle.position.x + half, particle.position.y + half},
                                        color,
                                        {texture_max, texture_max});
        vertices[vertex++] = sf::Vertex({particle.position.x - half, particle.position.y + half},
                                        color,
                                        {0.0f, texture_max});
    }

    density_target_->setView(sf::View(camera_view));
    density_target_->clear(sf::Color::Transparent);
    sf::RenderStates states;
    states.texture = particle_texture_.get();
    states.blendMode = sf::BlendAdd;
    density_target_->draw(vertices, states);
    density_target_->display();
    return &density_target_->getTexture();
}

} // namespace pac::pnc
