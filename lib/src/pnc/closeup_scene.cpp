#include "engine/pnc/closeup_scene.hpp"

#include "engine/core/cursor.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/text_encoding.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Window/Event.hpp>
#include <sol/sol.hpp>

#include <algorithm>
#include <exception>

namespace pac::pnc {

namespace {
const sf::Color kCaptionColor(235, 235, 240);
const sf::Color kDefaultSpeechColor(230, 230, 230);
// Where a scripted close-up's speech floats: bottom-centre of the virtual screen
// (place_speech raises the balloon above this point, subtitle-like).
constexpr float kTalkAnchorY = 0.86f; // fraction of the virtual height
} // namespace

struct CloseUpScene::Impl {
    sol::object prev_talk; // the `talk` global we shadow while scripted; restored on leave
};

CloseUpScene::CloseUpScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx) {
    data_path_ = params.get_or("data", "");
    logic_path_ = params.get_or("logic", "");
    cast_path_ = params.get_or("cast", "");
    on_exit_ = params.get_or("on_exit", "");
    const std::string font_path = params.get_or("font", "");
    if (!font_path.empty()) {
        font_ = ctx_.resources.try_font(font_path);
    }
    // A close-up is a HUD-like full-screen view that fully covers the scene it is
    // pushed over, so it stays opaque (the default) and the manager skips drawing
    // the room beneath it.
}

CloseUpScene::~CloseUpScene() = default;

void CloseUpScene::enter() {
    if (data_path_.empty()) {
        ctx_.log.error("CloseUp: no 'data' parameter");
        return;
    }
    try {
        data_ = parse_closeup(ctx_.resources.read_text(data_path_));
        loaded_ = true;
    } catch (const std::exception& e) {
        ctx_.log.error(std::string("CloseUp: ") + e.what());
    }

    // Optional cast, for per-speaker speech colors in scripted talk.
    if (!cast_path_.empty()) {
        try {
            cast_ = parse_cast(ctx_.resources.read_text(cast_path_));
            has_cast_ = true;
        } catch (const std::exception& e) {
            ctx_.log.warn(std::string("CloseUp: cast '" + cast_path_ + "': ") + e.what());
        }
    }

    // A scope for this close-up's scripted tasks (hotspot handlers, on_enter spawns).
    // Cancelled on leave, so its coroutines never outlive the view.
    closeup_scope_ = ctx_.scripting.open_scope();

    // Optional Lua sidecar -> a scripted close-up.
    if (!logic_path_.empty()) {
        std::string code;
        try {
            code = ctx_.resources.read_text(logic_path_);
        } catch (const std::exception& e) {
            ctx_.log.error(std::string("CloseUp: logic '" + logic_path_ + "': ") + e.what());
        }
        if (!code.empty() && runtime_.load(ctx_.scripting, code, logic_path_, ctx_.log)) {
            scripted_ = true;
        }
    }

    if (scripted_) {
        impl_ = std::make_unique<Impl>();
        sol::state& L = ctx_.scripting.lua();
        // Shadow the room's `talk` with a close-up-local one so scripted lines show
        // in THIS view's speech bubble; restored in leave(). Room-mutating globals
        // (spawn_npc / despawn_npc / set_state / ...) are left bound to the live
        // RoomScene, which is the reach-through used for the animation workaround.
        impl_->prev_talk = L["talk"];
        L.set_function("_closeup_talk_start", [this](std::string speaker, std::string text) {
            return api_talk(speaker, text);
        });
        ctx_.scripting.run_string("function talk(speaker, text)\n"
                                  "  local ev = _closeup_talk_start(speaker, text)\n"
                                  "  local _, ismain = coroutine.running()\n"
                                  "  if ev and ev ~= '' and not ismain then wait_event(ev) end\n"
                                  "end\n",
                                  "=closeup_talk");

        // Close-up-local authoring primitives (unbound in leave, since the lambdas
        // capture `this`): rename a hotspot's hover/look label, and set the
        // off-screen "shout" banner. `shout()` / `shout("")` clears it.
        L.set_function("set_hotspot_name", [this](std::string id, std::string name) {
            hotspot_names_[id] = std::move(name);
        });
        L.set_function("shout", [this](sol::optional<std::string> text) {
            shout_text_ = text.value_or(std::string());
        });

        // on_enter runs with the close-up scope current, so a spawn(...) inside it
        // lands here. Restore the global scope afterwards (the scheduler sets the
        // scope per task on resume).
        ctx_.scripting.set_current_scope(closeup_scope_);
        runtime_.run_on_enter();
        ctx_.scripting.set_current_scope(ctx_.scripting.global_scope());
    }
}

void CloseUpScene::leave() {
    if (scripted_) {
        ctx_.scripting.set_current_scope(closeup_scope_);
        runtime_.run_on_exit();
        // Restore the previous `talk` and drop our binding (its lambda captured
        // `this`, which is about to be destroyed).
        sol::state& L = ctx_.scripting.lua();
        if (impl_) {
            L["talk"] = impl_->prev_talk;
        }
        L["_closeup_talk_start"] = sol::lua_nil;
        L["set_hotspot_name"] = sol::lua_nil;
        L["shout"] = sol::lua_nil;
        ctx_.scripting.set_current_scope(ctx_.scripting.global_scope());
    }
    // Reap the scope: cancels any in-flight hotspot handler / spawned task.
    ctx_.scripting.cancel_scope(closeup_scope_);
}

void CloseUpScene::exit() {
    if (on_exit_.empty()) {
        ctx_.scenes.pop_scene(); // back to the scene we overlaid
    } else {
        ctx_.scenes.goto_scene(on_exit_);
    }
}

std::string CloseUpScene::api_talk(const std::string& speaker, const std::string& text) {
    sf::Color color = kDefaultSpeechColor;
    if (has_cast_) {
        if (const Character* c = cast_.character(speaker)) {
            color = c->speech_color;
        }
    }
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const geom::Point anchor{static_cast<float>(vres.x) / 2.0f,
                             static_cast<float>(vres.y) * kTalkAnchorY};
    float duration = 0.5f + 0.06f * static_cast<float>(text.size());
    duration = std::clamp(duration, 1.0f, 7.0f);
    speech_.show(text, anchor, color, duration, 48.0f);
    if (!speech_.active()) {
        return std::string(); // empty text -> nothing shown -> never wait
    }
    const std::string event = "__closeup_spoke." + std::to_string(++talk_seq_);
    pending_speech_.push_back({closeup_scope_, event});
    return event;
}

std::string CloseUpScene::display_name(const CloseUpHotspot& hs) const {
    const auto it = hotspot_names_.find(hs.id);
    return it != hotspot_names_.end() ? it->second : hs.name;
}

void CloseUpScene::activate(const CloseUpHotspot& hs) {
    // A scripted handler wins. Ignore a new activation while one is still running
    // (so a multi-step handler isn't interrupted by another click).
    if (scripted_ && runtime_.has_hotspot(hs.id)) {
        if (active_handler_ != 0 && ctx_.scripting.is_task_alive(active_handler_)) {
            return;
        }
        active_handler_ = runtime_.spawn_hotspot(ctx_.scripting, closeup_scope_, hs.id);
        ctx_.scripting.set_current_scope(ctx_.scripting.global_scope());
        return;
    }
    if (!hs.goto_scene.empty()) {
        ctx_.scenes.goto_scene(hs.goto_scene);
        return;
    }
    // The single look action: show the hotspot's (possibly overridden) name as a
    // caption at the cursor.
    const std::string name = display_name(hs);
    float duration = 0.5f + 0.06f * static_cast<float>(name.size());
    duration = std::clamp(duration, 1.0f, 7.0f);
    speech_.show(name, hover_, kCaptionColor, duration, 48.0f);
}

void CloseUpScene::handle_event(const sf::Event& event) {
    if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
        exit();
        return;
    }
    if (event.type == sf::Event::MouseMoved) {
        hover_ = {static_cast<float>(event.mouseMove.x), static_cast<float>(event.mouseMove.y)};
        return;
    }
    if (event.type == sf::Event::MouseButtonReleased) {
        if (event.mouseButton.button == sf::Mouse::Right) {
            exit();
            return;
        }
        if (event.mouseButton.button == sf::Mouse::Left) {
            // A click dismisses a showing caption/line first (like in-room speech) —
            // this is also how a scripted multi-step talk is advanced.
            if (speech_.active()) {
                speech_.skip();
                return;
            }
            if (loaded_) {
                if (const CloseUpHotspot* hs = data_.hotspot_at(hover_)) {
                    activate(*hs);
                }
            }
        }
    }
}

void CloseUpScene::update(float dt) {
    speech_.update(dt);
    // Wake any scripted talk(...) once its line has cleared (duration elapsed or
    // skipped) so the next line in a sequence runs (mirrors RoomScene).
    if (!speech_.active() && !pending_speech_.empty()) {
        for (const PendingSpeech& p : pending_speech_) {
            ctx_.scripting.emit(p.scope, p.event);
        }
        pending_speech_.clear();
    }
    hovered_ = loaded_ ? data_.hotspot_at(hover_) : nullptr;
    if (hovered_ != nullptr) {
        ctx_.cursor.want(pac::core::CursorKind::INTERACT);
    }
}

void CloseUpScene::draw(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const auto vw = static_cast<float>(vres.x);
    const auto vh = static_cast<float>(vres.y);

    sf::RectangleShape fill(sf::Vector2f(vw, vh));
    fill.setFillColor(data_.background_color);
    target.draw(fill);

    if (loaded_ && !data_.background.empty()) {
        try {
            const sf::Texture& tex = ctx_.resources.texture(data_.background);
            sf::Sprite sprite(tex);
            const sf::Vector2u ts = tex.getSize();
            if (ts.x > 0 && ts.y > 0) {
                sprite.setScale(vw / static_cast<float>(ts.x), vh / static_cast<float>(ts.y));
            }
            target.draw(sprite);
        } catch (const std::exception& e) {
            ctx_.log.error(e.what());
        }
    }

    if (font_ != nullptr) {
        // Off-screen "shout" banner (scripted `shout(text)`): a styled, word-wrapped
        // line across the very top, independent of the speech bubble. Drawn first so
        // the hover label can sit below it.
        float top_below = vh * 0.06f; // where the hover label goes by default
        if (!shout_text_.empty()) {
            const unsigned sz = 30;
            const auto measure = [&](const std::string& s) {
                return sf::Text(pac::core::utf8(s), *font_, sz).getLocalBounds().width;
            };
            const std::vector<std::string> lines = wrap_text(shout_text_, vw * 0.9f, measure);
            const float line_h = font_->getLineSpacing(sz);
            float y = vh * 0.04f;
            for (const std::string& ln : lines) {
                sf::Text t(pac::core::utf8(ln), *font_, sz);
                t.setFillColor(sf::Color(235, 70, 60));
                t.setOutlineColor(sf::Color(0, 0, 0, 220));
                t.setOutlineThickness(2.5f);
                const sf::FloatRect b = t.getLocalBounds();
                t.setPosition((vw - b.width) / 2.0f - b.left, y);
                target.draw(t);
                y += line_h;
            }
            top_below = y + vh * 0.02f; // push the hover label below the banner
        }

        // Hover affordance: the examined thing's (possibly renamed) name across the
        // top, unless a caption is already showing there.
        if (hovered_ != nullptr && !speech_.active()) {
            sf::Text label(pac::core::utf8(display_name(*hovered_)), *font_, 24);
            label.setFillColor(kCaptionColor);
            label.setOutlineColor(sf::Color(0, 0, 0, 200));
            label.setOutlineThickness(2.0f);
            const sf::FloatRect b = label.getLocalBounds();
            label.setPosition((vw - b.width) / 2.0f - b.left, top_below);
            target.draw(label);
        }

        sf::Text hint(pac::core::utf8("[Esc] " + ctx_.strings.ui_label("back")), *font_, 18);
        hint.setFillColor(sf::Color(200, 200, 210, 180));
        hint.setOutlineColor(sf::Color(0, 0, 0, 180));
        hint.setOutlineThickness(1.0f);
        hint.setPosition(16.0f, vh - 32.0f);
        target.draw(hint);
    }

    speech_.draw(target, font_);
}

} // namespace pac::pnc
