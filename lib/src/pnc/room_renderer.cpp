#include "engine/pnc/room_renderer.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/render_stats.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/geom/geometry.hpp"
#include "engine/gfx/shader_chain.hpp"
#include "engine/gfx/shader_effect.hpp"
#include "engine/pnc/avatar.hpp"
#include "engine/pnc/room.hpp"
#include "engine/pnc/room_runtime.hpp"

#include <SFML/Graphics/Glsl.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <SFML/Graphics/View.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace pac::pnc {

namespace {

// Set the reserved uniforms (only the ones a program declares — SFML logs on a
// stray setUniform) plus the author's params. Used by the single-shader fast
// path; the multi-pass chain handles its own RT-sized resolution.
void bind_single_pass(sf::Shader& shader,
                      pac::core::ShaderProgram& program,
                      const gfx::ShaderEffect& fx,
                      const sf::Texture& texture,
                      float time) {
    if (program.uses_time) {
        shader.setUniform("u_time", time);
    }
    if (program.uses_resolution) {
        shader.setUniform("u_resolution",
                          sf::Glsl::Vec2(static_cast<float>(texture.getSize().x),
                                         static_cast<float>(texture.getSize().y)));
    }
    if (program.uses_texture) {
        shader.setUniform("texture", sf::Shader::CurrentTexture);
    }
    gfx::apply_shader_params(shader, fx.params);
}

// Returns the first enabled, non-controller effect in `effects`, or nullptr —
// the controller path is design-for (warned once at load) and falls back to
// unshaded. Used to decide the single-shader fast path.
const gfx::ShaderEffect* first_applicable(const std::vector<gfx::ShaderEffect>& effects) {
    for (const gfx::ShaderEffect& fx : effects) {
        if (fx.enabled && fx.controller.empty()) {
            return &fx;
        }
    }
    return nullptr;
}

// Count of `effects` that the engine can apply right now: enabled, no controller.
std::size_t applicable_count(const std::vector<gfx::ShaderEffect>& effects) {
    std::size_t n = 0;
    for (const gfx::ShaderEffect& fx : effects) {
        if (fx.enabled && fx.controller.empty()) {
            ++n;
        }
    }
    return n;
}

// Draw a textured sprite (`tex` sub-rect = `sub`) at world `position`, optionally
// uniformly scaled (about the position), with the given shader stack. Handles all
// three render paths: no shader, single shader (fast path — one direct draw), and
// multi-pass (ping-pong via `chain`).
void draw_shaded_sprite(sf::RenderTarget& target,
                        gfx::ShaderChain& chain,
                        pac::core::ResourceCache& resources,
                        const sf::Texture& tex,
                        const sf::IntRect& sub,
                        sf::Vector2f position,
                        float uniform_scale,
                        const std::vector<gfx::ShaderEffect>& effects,
                        float time) {
    const std::size_t n = applicable_count(effects);
    if (n <= 1) {
        sf::Sprite sprite(tex, sub);
        sprite.setPosition(position);
        if (uniform_scale != 1.0f) {
            sprite.setScale(uniform_scale, uniform_scale);
        }
        sf::RenderStates states;
        if (n == 1) {
            const gfx::ShaderEffect* fx = first_applicable(effects);
            if (pac::core::ShaderProgram* program = resources.shader(fx->source)) {
                bind_single_pass(program->shader, *program, *fx, tex, time);
                states.shader = &program->shader;
                // Profiling (#112): the single-effect fast path is a real shader
                // draw (no ping-pong FBO, so no RT VRAM). Count it like a pass.
                pac::core::note_shader_passes(1);
            }
        }
        target.draw(sprite, states);
        return;
    }
    const sf::Texture* result = chain.apply(resources, tex, sub, effects, time);
    if (!result) {
        // All effects failed (missing source, unsupported GPU). Fall back unshaded.
        sf::Sprite sprite(tex, sub);
        sprite.setPosition(position);
        if (uniform_scale != 1.0f) {
            sprite.setScale(uniform_scale, uniform_scale);
        }
        target.draw(sprite);
        return;
    }
    sf::Sprite blit(*result, sf::IntRect(0, 0, sub.width, sub.height));
    blit.setPosition(position);
    if (uniform_scale != 1.0f) {
        blit.setScale(uniform_scale, uniform_scale);
    }
    target.draw(blit);
}

} // namespace

void RoomRenderer::draw(sf::RenderTarget& target,
                        const RoomRuntime& room,
                        const std::string& room_dir,
                        pac::core::ResourceCache& resources,
                        const Avatar* player,
                        const std::vector<const Avatar*>& npcs,
                        pac::core::Diagnostics& log,
                        const ShaderEnv& shaders) const {
    const RoomData& data = room.data();
    const float shader_time = shaders.time;

    // Solid backdrop behind every layer: fill the visible view so any world area
    // a layer doesn't cover (within or beyond the layer bounds) shows the color.
    const sf::View& view = target.getView();
    sf::RectangleShape fill(view.getSize());
    fill.setPosition(view.getCenter() - view.getSize() / 2.0f);
    fill.setFillColor(data.background_color);
    target.draw(fill);

    using DrawFn = std::function<void(sf::RenderTarget&)>;
    std::vector<std::pair<float, DrawFn>> items;

    // Background layers draw at native pixel size × the layer's uniform `scale`
    // (aspect-preserving) with the top-left at the layer's `origin` (default
    // (0,0)), so layers may differ in size and be placed freely. The room's world
    // bounds are the union of these rects.
    for (const BackgroundLayer& layer : data.layers) {
        if (!layer.id.empty() && !room.layer_visible(layer.id)) {
            continue;
        }
        const std::string image = pac::core::logical_join(room_dir, layer.image);
        const geom::Point origin = layer.origin;
        const float scale = layer.scale;
        const std::vector<gfx::ShaderEffect>* fx = &layer.shaders;
        items.emplace_back(
            layer.z,
            [this, &resources, &log, image, origin, scale, fx, shader_time](sf::RenderTarget& t) {
                try {
                    const sf::Texture& tex = resources.texture(image);
                    const sf::IntRect full(0,
                                           0,
                                           static_cast<int>(tex.getSize().x),
                                           static_cast<int>(tex.getSize().y));
                    draw_shaded_sprite(t,
                                       chain_,
                                       resources,
                                       tex,
                                       full,
                                       sf::Vector2f(origin.x, origin.y),
                                       scale,
                                       *fx,
                                       shader_time);
                } catch (const std::exception& e) {
                    log.error(e.what());
                }
            });
    }

    // Regions: the current state's image at the area's top-left. Depth, in order
    // of priority: an explicit `baseline` (a world-Y ground line that sorts
    // against avatar feet, for a perspective region), else `over: <layer>` (pin to
    // a named layer's z), else the explicit `z` (design 04 §Z-order). A region
    // with `over: <layer>` also inherits that layer's shaders (issue #105):
    // layer effects run first so the region matches the surrounding background,
    // and region-specific effects (e.g. a local glow) run on top.
    const auto find_layer = [&data](const std::string& layer_id) -> const BackgroundLayer* {
        for (const BackgroundLayer& l : data.layers) {
            if (l.id == layer_id) {
                return &l;
            }
        }
        return nullptr;
    };
    for (const auto& [id, region] : data.regions) {
        const auto state_it = region.states.find(room.region_state(id));
        if (state_it == region.states.end() || state_it->second.empty()) {
            continue;
        }
        // Resolve the "over" layer first so shader inheritance is independent of
        // the depth source (baseline still overrides z, but the layer's effects
        // still get inherited if the author declared `over:`).
        const BackgroundLayer* over_layer = region.over.empty() ? nullptr : find_layer(region.over);
        if (!region.over.empty() && !over_layer) {
            log.error("region '" + id + "': over names unknown layer '" + region.over + "'");
        }
        float z = region.z;
        if (region.baseline) {
            z = *region.baseline;
        } else if (over_layer) {
            z = over_layer->z;
        }

        // Compose the effective shader stack: inherited layer effects first, then
        // region-specific. A region without `over:` simply uses its own.
        std::vector<gfx::ShaderEffect> effective;
        if (over_layer && !over_layer->shaders.empty()) {
            effective = over_layer->shaders;
        }
        effective.insert(effective.end(), region.shaders.begin(), region.shaders.end());

        const std::string image = pac::core::logical_join(room_dir, state_it->second);
        const sf::FloatRect bounds = geom::polygon_bounds(region.area);
        items.emplace_back(
            z,
            [this, &resources, &log, image, bounds, effective, shader_time](sf::RenderTarget& t) {
                try {
                    const sf::Texture& tex = resources.texture(image);
                    const sf::IntRect full(0,
                                           0,
                                           static_cast<int>(tex.getSize().x),
                                           static_cast<int>(tex.getSize().y));
                    draw_shaded_sprite(t,
                                       chain_,
                                       resources,
                                       tex,
                                       full,
                                       sf::Vector2f(bounds.left, bounds.top),
                                       1.0f,
                                       effective,
                                       shader_time);
                } catch (const std::exception& e) {
                    log.error(e.what());
                }
            });
    }

    // Objects: visible ones at their position. Sort depth, in order of priority:
    // an explicit `baseline` (a world-Y ground line, sorts against avatar feet),
    // else the sprite's bottom edge for `z: auto`, else the fixed `z`.
    for (const auto& [id, object] : data.objects) {
        if (!room.object_visible(id) || object.sprite.empty()) {
            continue;
        }
        // Animated object (#142): its AnimatedSprite is advanced + transform-synced
        // by RoomRuntime::update_objects; draw the current frame. z from the live
        // frame bounds (scaled bottom edge) unless an explicit baseline/z is set.
        if (const gfx::AnimatedSprite* spr =
                room.object_animated(id) ? room.object_sprite(id) : nullptr) {
            const sf::FloatRect b = spr->global_bounds();
            float z = object.baseline ? *object.baseline
                      : object.z_auto ? b.top + b.height
                                      : object.z;
            items.emplace_back(z, [spr, &resources, shader_time, this](sf::RenderTarget& t) {
                spr->draw(t, resources, shader_time, &chain_);
            });
            continue;
        }
        const std::string image = pac::core::logical_join(room_dir, object.sprite);
        // Position/scale come from the runtime pose (scriptable move/resize, #142),
        // falling back to the def for objects never touched by script.
        const float obj_scale = room.object_scale(id);
        const geom::Point pos = room.object_position(id);
        float z = object.baseline ? *object.baseline : object.z;
        try {
            const sf::Texture& tex = resources.texture(image);
            if (!object.baseline && object.z_auto) {
                z = pos.y + static_cast<float>(tex.getSize().y) * obj_scale;
            }
        } catch (const std::exception& e) {
            log.error(e.what());
            continue;
        }
        const std::vector<gfx::ShaderEffect>* fx = &object.shaders;
        items.emplace_back(
            z,
            [this, &resources, &log, image, pos, obj_scale, fx, shader_time](sf::RenderTarget& t) {
                try {
                    const sf::Texture& tex = resources.texture(image);
                    const sf::IntRect full(0,
                                           0,
                                           static_cast<int>(tex.getSize().x),
                                           static_cast<int>(tex.getSize().y));
                    draw_shaded_sprite(t,
                                       chain_,
                                       resources,
                                       tex,
                                       full,
                                       sf::Vector2f(pos.x, pos.y),
                                       obj_scale,
                                       *fx,
                                       shader_time);
                } catch (const std::exception& e) {
                    log.error(e.what());
                }
            });
    }

    // Walk-behind masks: a convex patch of a source layer redrawn on top at its
    // baseline (a world-Y line), so avatars sort in front of / behind it like any
    // baseline object — without duplicating the art (design 04 §Walk-behind). The
    // patch is a textured triangle fan whose texCoords map each world vertex back
    // to the source layer's texel ((world - origin) / scale). Shaders on the
    // source layer are not (yet) applied to the walk-behind — multi-pass would
    // need a polygon-aware RT path, which is out of scope here.
    for (const WalkBehind& wb : data.walkbehinds) {
        if (wb.area.size() < 3) {
            continue;
        }
        const BackgroundLayer* src = nullptr;
        for (const BackgroundLayer& l : data.layers) {
            if (l.id == wb.layer) {
                src = &l;
                break;
            }
        }
        if (!src) {
            log.error("walkbehind '" + wb.id + "': unknown layer '" + wb.layer + "'");
            continue;
        }
        const std::string image = pac::core::logical_join(room_dir, src->image);
        const geom::Point origin = src->origin;
        const float scale = src->scale;
        sf::VertexArray fan(sf::TriangleFan, wb.area.size());
        for (std::size_t i = 0; i < wb.area.size(); ++i) {
            const geom::Point& p = wb.area[i];
            fan[i].position = sf::Vector2f(p.x, p.y);
            fan[i].texCoords = sf::Vector2f((p.x - origin.x) / scale, (p.y - origin.y) / scale);
        }
        items.emplace_back(wb.baseline,
                           [&resources, &log, image, fan = std::move(fan)](sf::RenderTarget& t) {
                               try {
                                   sf::RenderStates states;
                                   states.texture = &resources.texture(image);
                                   t.draw(fan, states);
                               } catch (const std::exception& e) {
                                   log.error(e.what());
                               }
                           });
    }

    if (player) {
        items.emplace_back(player->z(),
                           [player, &resources, shader_time, this](sf::RenderTarget& t) {
                               player->draw(t, resources, shader_time, &chain_);
                           });
    }
    for (const Avatar* npc : npcs) {
        if (npc) {
            items.emplace_back(npc->z(), [npc, &resources, shader_time, this](sf::RenderTarget& t) {
                npc->draw(t, resources, shader_time, &chain_);
            });
        }
    }

    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    for (const auto& [z, draw_fn] : items) {
        draw_fn(target);
    }
}

sf::Vector2u compute_room_bounds(const RoomData& data,
                                 const std::string& room_dir,
                                 pac::core::ResourceCache& resources,
                                 sf::Vector2f viewport,
                                 pac::core::Diagnostics& log) {
    // World is anchored at (0,0); only the right/bottom extents grow it, and it is
    // never smaller than the room view. A layer placed at a negative origin spills
    // off the top-left and is never scrolled to, rather than shifting the origin.
    float right = viewport.x;
    float bottom = viewport.y;
    for (const BackgroundLayer& layer : data.layers) {
        try {
            const sf::Texture& tex =
                resources.texture(pac::core::logical_join(room_dir, layer.image));
            const sf::Vector2u ts = tex.getSize();
            right = std::max(right, layer.origin.x + static_cast<float>(ts.x) * layer.scale);
            bottom = std::max(bottom, layer.origin.y + static_cast<float>(ts.y) * layer.scale);
        } catch (const std::exception& e) {
            log.error(e.what());
        }
    }
    return {static_cast<unsigned>(std::ceil(right)), static_cast<unsigned>(std::ceil(bottom))};
}

void warn_unsupported_shader_features(const RoomData& data, pac::core::Diagnostics& log) {
    const auto check = [&](const std::string& owner, const std::vector<gfx::ShaderEffect>& fx) {
        for (const gfx::ShaderEffect& e : fx) {
            if (!e.controller.empty()) {
                log.warn("room '" + data.id + "': " + owner + " shader references controller '" +
                         e.controller + "', which is design-for and not yet applied");
            }
        }
    };
    for (const BackgroundLayer& l : data.layers) {
        check("layer '" + l.id + "'", l.shaders);
    }
    for (const auto& [id, region] : data.regions) {
        check("region '" + id + "'", region.shaders);
    }
    for (const auto& [id, object] : data.objects) {
        check("object '" + id + "'", object.shaders);
    }
}

} // namespace pac::pnc
