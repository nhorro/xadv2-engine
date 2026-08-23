#include "engine/gfx/shader_chain.hpp"

#include "engine/core/render_stats.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/gfx/gles2_compat.hpp"

#include <SFML/Graphics/Glsl.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <algorithm>

namespace pac::gfx {

ShaderChain::ShaderChain() = default;

ShaderChain::~ShaderChain() {
    // Release this chain's contribution to the live RT-VRAM profiling counter.
    if (rt_bytes_ != 0) {
        pac::core::add_shader_rt_bytes(-static_cast<std::ptrdiff_t>(rt_bytes_));
    }
}

void ShaderChain::ensure_size(unsigned width, unsigned height) {
    if (rt_[0] && rt_width_ >= width && rt_height_ >= height) {
        return;
    }
    // Grow monotonically — a busy scene with a few different texture sizes
    // converges to one allocation that fits them all.
    rt_width_ = std::max(rt_width_, width);
    rt_height_ = std::max(rt_height_, height);
    for (auto& rt : rt_) {
        rt = std::make_unique<sf::RenderTexture>();
        if (!rt->create(rt_width_, rt_height_)) {
            rt.reset();
            continue;
        }
        configure_gles2_target(*rt);
        rt->setSmooth(false);
    }
    // Mirror the new pool size into the profiling counter (#112): two RGBA8 RTs.
    const std::size_t new_bytes = static_cast<std::size_t>(rt_width_) * rt_height_ * 4 * 2;
    pac::core::add_shader_rt_bytes(static_cast<std::ptrdiff_t>(new_bytes) -
                                   static_cast<std::ptrdiff_t>(rt_bytes_));
    rt_bytes_ = new_bytes;
}

const sf::Texture* ShaderChain::apply(pac::core::ResourceCache& resources,
                                      const sf::Texture& source,
                                      const sf::IntRect& source_rect,
                                      const std::vector<ShaderEffect>& effects,
                                      float time,
                                      const RuntimeShaderPass* prefix) {
    if ((effects.empty() && (!prefix || !prefix->shader)) || source_rect.width <= 0 ||
        source_rect.height <= 0) {
        return nullptr;
    }

    const unsigned w = static_cast<unsigned>(source_rect.width);
    const unsigned h = static_cast<unsigned>(source_rect.height);
    ensure_size(w, h);
    if (!rt_[0] || !rt_[1]) {
        return nullptr;
    }
    // The final texture may be scaled when blitted back into the scene/window.
    // Match source filtering so an opted-in painterly game does not reintroduce
    // nearest-neighbour shimmer at the intermediate render-target boundary.
    for (auto& rt : rt_) {
        rt->setSmooth(resources.smooth_textures());
    }

    // Blit `source_rect` of `source` into RT0 at (0,0)..(w,h). The RT is sized to
    // the largest source seen, so we constrain the view to the current sub-rect
    // so a smaller drawable doesn't pick up stale pixels around it.
    sf::View view(sf::FloatRect(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)));
    view.setViewport(sf::FloatRect(0.0f,
                                   0.0f,
                                   static_cast<float>(w) / static_cast<float>(rt_width_),
                                   static_cast<float>(h) / static_cast<float>(rt_height_)));

    sf::RenderTexture* src = rt_[0].get();
    sf::RenderTexture* dst = rt_[1].get();

    src->setView(view);
    src->clear(sf::Color::Transparent);
    {
        sf::Sprite blit(source, source_rect);
        src->draw(blit);
    }
    src->display();

    bool any_applied = false;
    if (prefix && prefix->shader) {
        if (prefix->bind) {
            prefix->bind(*prefix->shader);
        }
        dst->setView(view);
        dst->clear(sf::Color::Transparent);
        sf::Sprite blit(src->getTexture(),
                        sf::IntRect(0, 0, static_cast<int>(w), static_cast<int>(h)));
        sf::RenderStates states;
        states.shader = prefix->shader;
        dst->draw(blit, states);
        dst->display();

        std::swap(src, dst);
        any_applied = true;
        pac::core::note_shader_passes(1);
    }
    for (const ShaderEffect& fx : effects) {
        if (!fx.enabled || !fx.controller.empty()) {
            // Controller is a design-for escape hatch (warned at room load);
            // skipping it leaves the previous pass's output in `src`.
            continue;
        }
        pac::core::ShaderProgram* program = resources.shader(fx.source);
        if (!program) {
            continue; // missing / unsupported / compile error — logged once
        }
        sf::Shader& shader = program->shader;
        if (program->uses_time) {
            shader.setUniform("u_time", time);
        }
        if (program->uses_resolution) {
            shader.setUniform("u_resolution",
                              sf::Glsl::Vec2(static_cast<float>(w), static_cast<float>(h)));
        }
        if (program->uses_texture) {
            shader.setUniform("texture", sf::Shader::CurrentTexture);
        }
        apply_shader_params(shader, fx.params);

        dst->setView(view);
        dst->clear(sf::Color::Transparent);
        sf::Sprite blit(src->getTexture(),
                        sf::IntRect(0, 0, static_cast<int>(w), static_cast<int>(h)));
        sf::RenderStates states;
        states.shader = &shader;
        dst->draw(blit, states);
        dst->display();

        std::swap(src, dst);
        any_applied = true;
        pac::core::note_shader_passes(1); // profiling counter (#112)
    }

    if (!any_applied) {
        return nullptr;
    }
    return &src->getTexture();
}

} // namespace pac::gfx
