#include "engine/pnc/room_renderer.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/geom/geometry.hpp"
#include "engine/gfx/shader_effect.hpp"
#include "engine/pnc/avatar.hpp"
#include "engine/pnc/room.hpp"
#include "engine/pnc/room_runtime.hpp"

#include <SFML/Graphics/Glsl.hpp>
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

// Resolve the shader to draw a textured item with, with its uniforms set, or null
// to draw unshaded. MVP applies the first effect only; a `controller` effect is
// declarative-only for now and skipped (warned once at load). The reserved
// uniforms u_time / u_resolution / texture are set here; author params follow.
sf::Shader* prepare_shader(pac::core::ResourceCache& resources,
                           const std::vector<gfx::ShaderEffect>& effects,
                           const sf::Texture& texture,
                           float time) {
    if (effects.empty()) {
        return nullptr;
    }
    const gfx::ShaderEffect& fx = effects.front();
    if (!fx.enabled || !fx.controller.empty()) {
        return nullptr;
    }
    pac::core::ShaderProgram* program = resources.shader(fx.source);
    if (!program) {
        return nullptr;
    }
    sf::Shader& shader = program->shader;
    // Only set a reserved uniform the program actually declares — SFML logs an
    // error for setUniform on an absent/unused uniform (e.g. a static grade has
    // no u_time / u_resolution).
    if (program->uses_time) {
        shader.setUniform("u_time", time);
    }
    if (program->uses_resolution) {
        shader.setUniform("u_resolution",
                          sf::Glsl::Vec2(static_cast<float>(texture.getSize().x),
                                         static_cast<float>(texture.getSize().y)));
    }
    if (program->uses_texture) {
        shader.setUniform("texture", sf::Shader::CurrentTexture);
    }
    gfx::apply_shader_params(shader, fx.params);
    return &shader;
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
            [&resources, &log, image, origin, scale, fx, shader_time](sf::RenderTarget& t) {
                try {
                    const sf::Texture& tex = resources.texture(image);
                    sf::Sprite sprite(tex);
                    sprite.setPosition(origin.x, origin.y);
                    if (scale != 1.0f) {
                        sprite.setScale(scale, scale);
                    }
                    sf::RenderStates states;
                    states.shader = prepare_shader(resources, *fx, tex, shader_time);
                    t.draw(sprite, states);
                } catch (const std::exception& e) {
                    log.error(e.what());
                }
            });
    }

    // Regions: the current state's image at the area's top-left. Depth, in order
    // of priority: an explicit `baseline` (a world-Y ground line that sorts
    // against avatar feet, for a perspective region), else `over: <layer>` (pin to
    // a named layer's z), else the explicit `z` (design 04 §Z-order).
    const auto layer_z = [&data](const std::string& layer_id) -> std::optional<float> {
        for (const BackgroundLayer& l : data.layers) {
            if (l.id == layer_id) {
                return l.z;
            }
        }
        return std::nullopt;
    };
    for (const auto& [id, region] : data.regions) {
        const auto state_it = region.states.find(room.region_state(id));
        if (state_it == region.states.end() || state_it->second.empty()) {
            continue;
        }
        float z = region.z;
        if (region.baseline) {
            z = *region.baseline;
        } else if (!region.over.empty()) {
            if (const auto lz = layer_z(region.over)) {
                z = *lz;
            } else {
                log.error("region '" + id + "': over names unknown layer '" + region.over + "'");
            }
        }
        const std::string image = pac::core::logical_join(room_dir, state_it->second);
        const sf::FloatRect bounds = geom::polygon_bounds(region.area);
        const std::vector<gfx::ShaderEffect>* fx = &region.shaders;
        items.emplace_back(z,
                           [&resources, &log, image, bounds, fx, shader_time](sf::RenderTarget& t) {
                               try {
                                   const sf::Texture& tex = resources.texture(image);
                                   sf::Sprite sprite(tex);
                                   sprite.setPosition(bounds.left, bounds.top);
                                   sf::RenderStates states;
                                   states.shader = prepare_shader(resources, *fx, tex, shader_time);
                                   t.draw(sprite, states);
                               } catch (const std::exception& e) {
                                   log.error(e.what());
                               }
                           });
    }

    // Objects: visible ones at their position. Sort depth, in order of priority:
    // an explicit `baseline` (a world-Y ground line, sorts against avatar feet),
    // else the sprite's bottom edge for `z: auto`, else the fixed `z`.
    for (const auto& [id, object] : data.objects) {
        if (!room.object_visible(id) || object.image.empty()) {
            continue;
        }
        const std::string image = pac::core::logical_join(room_dir, object.image);
        float z = object.baseline ? *object.baseline : object.z;
        try {
            const sf::Texture& tex = resources.texture(image);
            if (!object.baseline && object.z_auto) {
                z = object.position.y + static_cast<float>(tex.getSize().y);
            }
        } catch (const std::exception& e) {
            log.error(e.what());
            continue;
        }
        const geom::Point pos = object.position;
        const std::vector<gfx::ShaderEffect>* fx = &object.shaders;
        items.emplace_back(z, [&resources, &log, image, pos, fx, shader_time](sf::RenderTarget& t) {
            try {
                const sf::Texture& tex = resources.texture(image);
                sf::Sprite sprite(tex);
                sprite.setPosition(pos.x, pos.y);
                sf::RenderStates states;
                states.shader = prepare_shader(resources, *fx, tex, shader_time);
                t.draw(sprite, states);
            } catch (const std::exception& e) {
                log.error(e.what());
            }
        });
    }

    // Walk-behind masks: a convex patch of a source layer redrawn on top at its
    // baseline (a world-Y line), so avatars sort in front of / behind it like any
    // baseline object — without duplicating the art (design 04 §Walk-behind). The
    // patch is a textured triangle fan whose texCoords map each world vertex back
    // to the source layer's texel ((world - origin) / scale).
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
        items.emplace_back(player->z(), [player](sf::RenderTarget& t) { player->draw(t); });
    }
    for (const Avatar* npc : npcs) {
        if (npc) {
            items.emplace_back(npc->z(), [npc](sf::RenderTarget& t) { npc->draw(t); });
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
        if (fx.size() > 1) {
            log.warn("room '" + data.id + "': " + owner +
                     " declares a multi-shader stack; only the first is applied (multi-pass is "
                     "design-for)");
        }
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
