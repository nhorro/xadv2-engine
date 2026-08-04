#pragma once

#include "engine/gfx/shader_effect.hpp"

#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTexture.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace sf {
class Texture;
}

namespace pac::core {
class ResourceCache;
}

namespace pac::gfx {

/// Multi-pass fragment-shader applier. The chain renders a textured rect into an
/// off-screen render texture and ping-pongs it through `effects` in order: each
/// pass samples the previous pass's output as `texture`, with the reserved
/// uniforms `u_time` / `u_resolution` / `texture` set automatically when the
/// shader declares them. The result is a `sf::Texture` the caller draws as a
/// normal sprite at whatever world transform the drawable demands — the chain is
/// transform-agnostic, so layers (scaled at the origin), regions (placed at the
/// polygon's bounds top-left), objects, animated sprites, and a room's composed
/// scenery post-process all share it.
///
/// `apply` reuses two pooled render textures sized to the largest source seen,
/// so a steady scene reaches a steady allocation; an effect that fails to load
/// (missing source, GPU has no shader support, compile error) is logged once by
/// the resource cache and skipped here (the chain still runs the rest, like the
/// single-pass path). Returns nullptr when there are no enabled effects, or when
/// none of them could be applied — callers fall back to drawing unshaded.
class ShaderChain {
public:
    ShaderChain();
    ~ShaderChain();

    ShaderChain(const ShaderChain&) = delete;
    ShaderChain& operator=(const ShaderChain&) = delete;

    /// Apply `effects` to `source_rect` of `source`, returning the final render
    /// texture (caller draws it as a sprite). Pass `time` for `u_time`.
    const sf::Texture* apply(pac::core::ResourceCache& resources,
                             const sf::Texture& source,
                             const sf::IntRect& source_rect,
                             const std::vector<ShaderEffect>& effects,
                             float time);

private:
    void ensure_size(unsigned width, unsigned height);

    std::unique_ptr<sf::RenderTexture> rt_[2];
    unsigned rt_width_ = 0;
    unsigned rt_height_ = 0;
    // This chain's current render-target VRAM contribution (both pooled RTs),
    // mirrored into the process-wide profiling counter (#112) on resize/destroy.
    std::size_t rt_bytes_ = 0;
};

} // namespace pac::gfx
