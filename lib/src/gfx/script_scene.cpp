#include "engine/gfx/script_scene.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/scene_factory.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/scripting.hpp"
#include "engine/gfx/asset_error.hpp"
#include "engine/gfx/visual_sprite.hpp"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/String.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>
#include <sol/sol.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <type_traits>
#include <utility>
#include <variant>

namespace pac::gfx {

namespace {

constexpr const char* kSource = "script-scene-loader";

[[noreturn]] void script_scene_fail(const std::string& code,
                                    const std::string& message,
                                    const YAML::Node& at = YAML::Node()) {
    pac::core::fail_at<AssetError>(kSource, code, message, at);
}

float finite_number(const YAML::Node& node, const std::string& field, float fallback) {
    if (!node) {
        return fallback;
    }
    float value = 0.0f;
    try {
        value = node.as<float>();
    } catch (const YAML::Exception&) {
        script_scene_fail("script-scene.number-invalid", "'" + field + "' must be a number", node);
    }
    if (!std::isfinite(value)) {
        script_scene_fail("script-scene.number-invalid", "'" + field + "' must be finite", node);
    }
    return value;
}

std::string scalar_string(const YAML::Node& node, const std::string& field) {
    if (!node || !node.IsScalar()) {
        script_scene_fail("script-scene.string-invalid", "'" + field + "' must be a string", node);
    }
    try {
        return node.as<std::string>();
    } catch (const YAML::Exception&) {
        script_scene_fail("script-scene.string-invalid", "'" + field + "' must be a string", node);
    }
}

sf::Vector2f parse_vector(const YAML::Node& node, const std::string& field, sf::Vector2f fallback) {
    if (!node) {
        return fallback;
    }
    if (!node.IsMap() || !node["x"] || !node["y"]) {
        script_scene_fail("script-scene.vector-invalid",
                          "'" + field + "' must be a mapping with x and y",
                          node);
    }
    return {finite_number(node["x"], field + ".x", fallback.x),
            finite_number(node["y"], field + ".y", fallback.y)};
}

sf::Color parse_color(const YAML::Node& node, const std::string& field, sf::Color fallback) {
    if (!node) {
        return fallback;
    }
    if (!node.IsMap() || !node["r"] || !node["g"] || !node["b"]) {
        script_scene_fail("script-scene.color-invalid",
                          "'" + field + "' must contain r, g, and b",
                          node);
    }
    const auto channel = [&](const char* name, sf::Uint8 default_value) {
        int value = default_value;
        try {
            value = node[name] ? node[name].as<int>() : default_value;
        } catch (const YAML::Exception&) {
            script_scene_fail("script-scene.color-invalid",
                              "'" + field + "." + name + "' must be an integer",
                              node[name]);
        }
        if (value < 0 || value > 255) {
            script_scene_fail("script-scene.color-invalid",
                              "'" + field + "." + name + "' must be between 0 and 255",
                              node[name]);
        }
        return static_cast<sf::Uint8>(value);
    };
    return {channel("r", fallback.r),
            channel("g", fallback.g),
            channel("b", fallback.b),
            channel("a", fallback.a)};
}

std::string
resolve_asset(const std::string& raw, const std::string& logical_path, const YAML::Node& at) {
    if (raw.empty()) {
        script_scene_fail("script-scene.asset-path-empty", "asset path must not be empty", at);
    }
    std::string resolved;
    if (!raw.empty() && raw.front() == '/') {
        resolved = raw.substr(1);
    } else if (logical_path.empty()) {
        resolved = raw;
    } else {
        resolved = pac::core::logical_join(pac::core::logical_dir(logical_path), raw);
    }
    if (!pac::core::is_valid_logical_path(resolved)) {
        script_scene_fail("script-scene.asset-path-invalid",
                          "'" + raw + "' did not resolve to a valid logical path",
                          at);
    }
    return resolved;
}

std::string key_name(sf::Keyboard::Key key) {
    if (key >= sf::Keyboard::A && key <= sf::Keyboard::Z) {
        return std::string(1, static_cast<char>('a' + (key - sf::Keyboard::A)));
    }
    if (key >= sf::Keyboard::Num0 && key <= sf::Keyboard::Num9) {
        return std::string(1, static_cast<char>('0' + (key - sf::Keyboard::Num0)));
    }
    if (key >= sf::Keyboard::Numpad0 && key <= sf::Keyboard::Numpad9) {
        return "numpad_" + std::to_string(key - sf::Keyboard::Numpad0);
    }
    if (key >= sf::Keyboard::F1 && key <= sf::Keyboard::F15) {
        return "f" + std::to_string(1 + key - sf::Keyboard::F1);
    }
    switch (key) {
    case sf::Keyboard::Escape:
        return "escape";
    case sf::Keyboard::LControl:
        return "left_control";
    case sf::Keyboard::LShift:
        return "left_shift";
    case sf::Keyboard::LAlt:
        return "left_alt";
    case sf::Keyboard::LSystem:
        return "left_system";
    case sf::Keyboard::RControl:
        return "right_control";
    case sf::Keyboard::RShift:
        return "right_shift";
    case sf::Keyboard::RAlt:
        return "right_alt";
    case sf::Keyboard::RSystem:
        return "right_system";
    case sf::Keyboard::Menu:
        return "menu";
    case sf::Keyboard::LBracket:
        return "left_bracket";
    case sf::Keyboard::RBracket:
        return "right_bracket";
    case sf::Keyboard::Semicolon:
        return "semicolon";
    case sf::Keyboard::Comma:
        return "comma";
    case sf::Keyboard::Period:
        return "period";
    case sf::Keyboard::Quote:
        return "quote";
    case sf::Keyboard::Slash:
        return "slash";
    case sf::Keyboard::Backslash:
        return "backslash";
    case sf::Keyboard::Tilde:
        return "tilde";
    case sf::Keyboard::Equal:
        return "equal";
    case sf::Keyboard::Dash:
        return "dash";
    case sf::Keyboard::Space:
        return "space";
    case sf::Keyboard::Enter:
        return "enter";
    case sf::Keyboard::BackSpace:
        return "backspace";
    case sf::Keyboard::Tab:
        return "tab";
    case sf::Keyboard::PageUp:
        return "page_up";
    case sf::Keyboard::PageDown:
        return "page_down";
    case sf::Keyboard::End:
        return "end";
    case sf::Keyboard::Home:
        return "home";
    case sf::Keyboard::Insert:
        return "insert";
    case sf::Keyboard::Delete:
        return "delete";
    case sf::Keyboard::Add:
        return "add";
    case sf::Keyboard::Subtract:
        return "subtract";
    case sf::Keyboard::Multiply:
        return "multiply";
    case sf::Keyboard::Divide:
        return "divide";
    case sf::Keyboard::Left:
        return "left";
    case sf::Keyboard::Right:
        return "right";
    case sf::Keyboard::Up:
        return "up";
    case sf::Keyboard::Down:
        return "down";
    case sf::Keyboard::Pause:
        return "pause";
    default:
        return "unknown";
    }
}

std::string button_name(sf::Mouse::Button button) {
    switch (button) {
    case sf::Mouse::Left:
        return "left";
    case sf::Mouse::Right:
        return "right";
    case sf::Mouse::Middle:
        return "middle";
    case sf::Mouse::XButton1:
        return "x1";
    case sf::Mouse::XButton2:
        return "x2";
    default:
        return "unknown";
    }
}

template <typename T>
sol::object optional_object(sol::state& lua, const std::optional<T>& value) {
    return value ? sol::make_object(lua, *value) : sol::make_object(lua, sol::lua_nil);
}

sol::object vector_object(sol::state& lua, const std::optional<sf::Vector2f>& value) {
    if (!value) {
        return sol::make_object(lua, sol::lua_nil);
    }
    return sol::make_object(lua, lua.create_table_with("x", value->x, "y", value->y));
}

sol::object bounds_object(sol::state& lua, const std::optional<sf::FloatRect>& value) {
    if (!value) {
        return sol::make_object(lua, sol::lua_nil);
    }
    return sol::make_object(lua,
                            lua.create_table_with("x",
                                                  value->left,
                                                  "y",
                                                  value->top,
                                                  "width",
                                                  value->width,
                                                  "height",
                                                  value->height));
}

} // namespace

ScriptSceneData parse_script_scene(const std::string& yaml_text,
                                   const std::string& expected_id,
                                   const std::string& logical_path) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        script_scene_fail("script-scene.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }
    if (!root || !root.IsMap()) {
        script_scene_fail("script-scene.root-not-map", "root must be a mapping", root);
    }

    ScriptSceneData data;
    try {
        data.version = root["version"] ? root["version"].as<int>() : 1;
        data.id = root["id"] ? root["id"].as<std::string>() : expected_id;
        data.opaque = root["opaque"] ? root["opaque"].as<bool>() : true;
    } catch (const YAML::Exception& e) {
        script_scene_fail("script-scene.field-invalid", e.what(), root);
    }
    if (data.id.empty()) {
        script_scene_fail("script-scene.id-missing", "'id' is required", root);
    }
    if (!expected_id.empty() && data.id != expected_id) {
        script_scene_fail("script-scene.id-mismatch",
                          "'id: " + data.id + "' does not match scene id '" + expected_id + "'",
                          root["id"]);
    }

    if (const YAML::Node background = root["background"]) {
        if (!background.IsMap()) {
            script_scene_fail("script-scene.background-invalid",
                              "'background' must be a mapping",
                              background);
        }
        data.background_color =
            parse_color(background["color"], "background.color", sf::Color::Black);
        if (background["image"]) {
            data.background_image =
                resolve_asset(scalar_string(background["image"], "background.image"),
                              logical_path,
                              background["image"]);
        }
    }

    const YAML::Node entities = root["entities"];
    if (entities && !entities.IsMap()) {
        script_scene_fail("script-scene.entities-not-map",
                          "'entities' must be a mapping",
                          entities);
    }
    std::set<std::string> ids;
    for (const auto& entry : entities ? entities : YAML::Node(YAML::NodeType::Map)) {
        ScriptSceneEntityData entity;
        entity.id = scalar_string(entry.first, "entity id");
        const YAML::Node node = entry.second;
        if (entity.id.empty()) {
            script_scene_fail("script-scene.entity-id-empty",
                              "entity id must not be empty",
                              entry.first);
        }
        if (!ids.insert(entity.id).second) {
            script_scene_fail("script-scene.entity-id-duplicate",
                              "duplicate entity id '" + entity.id + "'",
                              entry.first);
        }
        if (!node.IsMap()) {
            script_scene_fail("script-scene.entity-not-map",
                              "entity '" + entity.id + "' must be a mapping",
                              node);
        }
        if (const YAML::Node transform = node["transform"]) {
            if (!transform.IsMap()) {
                script_scene_fail("script-scene.transform-invalid",
                                  "entity '" + entity.id + "' transform must be a mapping",
                                  transform);
            }
            entity.transform.position =
                parse_vector(transform["position"], "transform.position", {0.0f, 0.0f});
            entity.transform.scale =
                parse_vector(transform["scale"], "transform.scale", {1.0f, 1.0f});
            entity.transform.rotation =
                finite_number(transform["rotation"], "transform.rotation", 0.0f);
        }
        entity.z = finite_number(node["z"], "z", 0.0f);
        try {
            entity.visible = node["visible"] ? node["visible"].as<bool>() : true;
        } catch (const YAML::Exception&) {
            script_scene_fail("script-scene.visible-invalid",
                              "'visible' must be boolean",
                              node["visible"]);
        }

        const YAML::Node sprite = node["sprite"];
        const YAML::Node animation = node["animation"];
        if (static_cast<bool>(sprite) == static_cast<bool>(animation)) {
            script_scene_fail("script-scene.visual-count-invalid",
                              "entity '" + entity.id +
                                  "' must declare exactly one of 'sprite' or 'animation'",
                              node);
        }
        if (sprite) {
            if (!sprite.IsMap() || !sprite["image"]) {
                script_scene_fail("script-scene.sprite-image-missing",
                                  "entity '" + entity.id + "' sprite needs 'image'",
                                  sprite);
            }
            ScriptSceneSprite component;
            component.image = resolve_asset(scalar_string(sprite["image"], "sprite.image"),
                                            logical_path,
                                            sprite["image"]);
            component.origin = parse_vector(sprite["origin"], "sprite.origin", {0.0f, 0.0f});
            component.tint = parse_color(sprite["tint"], "sprite.tint", sf::Color::White);
            entity.sprite = std::move(component);
        } else {
            if (!animation.IsMap() || !animation["source"] || !animation["sequence"]) {
                script_scene_fail("script-scene.animation-fields-missing",
                                  "entity '" + entity.id +
                                      "' animation needs 'source' and 'sequence'",
                                  animation);
            }
            ScriptSceneAnimation component;
            component.source = resolve_asset(scalar_string(animation["source"], "animation.source"),
                                             logical_path,
                                             animation["source"]);
            component.sequence = scalar_string(animation["sequence"], "animation.sequence");
            if (component.sequence.empty()) {
                script_scene_fail("script-scene.animation-sequence-empty",
                                  "animation sequence must not be empty",
                                  animation["sequence"]);
            }
            entity.animation = std::move(component);
        }
        data.entities.push_back(std::move(entity));
    }
    return data;
}

struct ScriptSceneApiState {
    ScriptScene* scene = nullptr;
};

struct ScriptEntityHandle {
    std::weak_ptr<ScriptSceneApiState> state;
    std::string id;

    ScriptScene* scene() const {
        const std::shared_ptr<ScriptSceneApiState> live = state.lock();
        return live ? live->scene : nullptr;
    }
    bool exists() const { return scene() && scene()->api_entity_exists(id); }
    sol::object position() const {
        ScriptScene* s = scene();
        return s ? vector_object(s->ctx_.scripting.lua(), s->api_position(id)) : sol::object();
    }
    ScriptEntityHandle& set_position(float x, float y) {
        if (ScriptScene* s = scene())
            s->api_set_position(id, {x, y});
        return *this;
    }
    ScriptEntityHandle& translate(float x, float y) {
        if (ScriptScene* s = scene())
            s->api_translate(id, {x, y});
        return *this;
    }
    sol::object scale() const {
        ScriptScene* s = scene();
        return s ? vector_object(s->ctx_.scripting.lua(), s->api_scale(id)) : sol::object();
    }
    ScriptEntityHandle& set_scale(float x, sol::optional<float> y) {
        if (ScriptScene* s = scene())
            s->api_set_scale(id, {x, y.value_or(x)});
        return *this;
    }
    sol::object rotation() const {
        ScriptScene* s = scene();
        return s ? optional_object(s->ctx_.scripting.lua(), s->api_rotation(id)) : sol::object();
    }
    ScriptEntityHandle& set_rotation(float degrees) {
        if (ScriptScene* s = scene())
            s->api_set_rotation(id, degrees);
        return *this;
    }
    sol::object z() const {
        ScriptScene* s = scene();
        return s ? optional_object(s->ctx_.scripting.lua(), s->api_z(id)) : sol::object();
    }
    ScriptEntityHandle& set_z(float z_value) {
        if (ScriptScene* s = scene())
            s->api_set_z(id, z_value);
        return *this;
    }
    sol::object visible() const {
        ScriptScene* s = scene();
        return s ? optional_object(s->ctx_.scripting.lua(), s->api_visible(id)) : sol::object();
    }
    ScriptEntityHandle& set_visible(bool value) {
        if (ScriptScene* s = scene())
            s->api_set_visible(id, value);
        return *this;
    }
    ScriptEntityHandle& show() { return set_visible(true); }
    ScriptEntityHandle& hide() { return set_visible(false); }
    ScriptEntityHandle& play(const std::string& sequence, sol::optional<bool> restart) {
        if (ScriptScene* s = scene())
            s->api_play(id, sequence, restart.value_or(true));
        return *this;
    }
    sol::object sequence() const {
        ScriptScene* s = scene();
        return s ? optional_object(s->ctx_.scripting.lua(), s->api_sequence(id)) : sol::object();
    }
    sol::object finished() const {
        ScriptScene* s = scene();
        return s ? optional_object(s->ctx_.scripting.lua(), s->api_finished(id)) : sol::object();
    }
    sol::object bounds() const {
        ScriptScene* s = scene();
        return s ? bounds_object(s->ctx_.scripting.lua(), s->api_bounds(id)) : sol::object();
    }
    bool hit_test(float x, float y) const { return scene() && scene()->api_hit_test(id, {x, y}); }
};

struct ScriptSceneContext {
    std::weak_ptr<ScriptSceneApiState> state;

    ScriptScene* scene() const {
        const std::shared_ptr<ScriptSceneApiState> live = state.lock();
        return live ? live->scene : nullptr;
    }
    ScriptEntityHandle entity(std::string id) const { return {state, std::move(id)}; }
    bool key_down(const std::string& key) const { return scene() && scene()->api_key_down(key); }
    sol::object pointer_position() const {
        ScriptScene* s = scene();
        if (!s)
            return sol::object();
        const sf::Vector2f p = s->api_pointer_position();
        sol::state& lua = s->ctx_.scripting.lua();
        return sol::make_object(lua, lua.create_table_with("x", p.x, "y", p.y));
    }
    bool pointer_down(const std::string& button) const {
        return scene() && scene()->api_pointer_down(button);
    }
    void goto_scene(const std::string& id) const {
        if (scene())
            scene()->api_goto_scene(id);
    }
    void push_scene(const std::string& id) const {
        if (scene())
            scene()->api_push_scene(id);
    }
    void pop_scene() const {
        if (scene())
            scene()->api_pop_scene();
    }
    void quit() const {
        if (scene())
            scene()->api_quit();
    }
};

namespace {

void bind_script_scene_types(sol::state& lua) {
    const sol::object marker = lua["__pac_script_scene_types_bound"];
    if (marker.valid() && marker.is<bool>() && marker.as<bool>()) {
        return;
    }
    lua.new_usertype<ScriptEntityHandle>(
        "ScriptEntity",
        sol::no_constructor,
        "id",
        sol::readonly_property(
            [](const ScriptEntityHandle& handle) -> const std::string& { return handle.id; }),
        "exists",
        &ScriptEntityHandle::exists,
        "position",
        &ScriptEntityHandle::position,
        "set_position",
        &ScriptEntityHandle::set_position,
        "translate",
        &ScriptEntityHandle::translate,
        "scale",
        &ScriptEntityHandle::scale,
        "set_scale",
        &ScriptEntityHandle::set_scale,
        "rotation",
        &ScriptEntityHandle::rotation,
        "set_rotation",
        &ScriptEntityHandle::set_rotation,
        "z",
        &ScriptEntityHandle::z,
        "set_z",
        &ScriptEntityHandle::set_z,
        "visible",
        &ScriptEntityHandle::visible,
        "set_visible",
        &ScriptEntityHandle::set_visible,
        "show",
        &ScriptEntityHandle::show,
        "hide",
        &ScriptEntityHandle::hide,
        "play",
        &ScriptEntityHandle::play,
        "sequence",
        &ScriptEntityHandle::sequence,
        "finished",
        &ScriptEntityHandle::finished,
        "bounds",
        &ScriptEntityHandle::bounds,
        "hit_test",
        &ScriptEntityHandle::hit_test);
    lua.new_usertype<ScriptSceneContext>("ScriptSceneContext",
                                         sol::no_constructor,
                                         "entity",
                                         &ScriptSceneContext::entity,
                                         "key_down",
                                         &ScriptSceneContext::key_down,
                                         "pointer_position",
                                         &ScriptSceneContext::pointer_position,
                                         "pointer_down",
                                         &ScriptSceneContext::pointer_down,
                                         "goto_scene",
                                         &ScriptSceneContext::goto_scene,
                                         "push_scene",
                                         &ScriptSceneContext::push_scene,
                                         "pop_scene",
                                         &ScriptSceneContext::pop_scene,
                                         "quit",
                                         &ScriptSceneContext::quit);
    lua["__pac_script_scene_types_bound"] = true;
}

} // namespace

struct ScriptScene::Impl {
    struct Entity {
        std::string id;
        ScriptSceneTransform transform;
        float z = 0.0f;
        bool visible = true;
        std::variant<sf::Sprite, VisualSprite> visual;
        bool animated = false;
    };

    ScriptSceneData data;
    std::vector<Entity> entities;
    std::map<std::string, std::size_t> by_id;
    std::optional<sf::Sprite> background;
    sol::table behavior;
    bool behavior_valid = false;
    std::set<std::string> disabled_callbacks;
    pac::core::ScopeId scope = 0;
    std::set<std::string> keys_down;
    std::set<std::string> pointer_buttons_down;
    sf::Vector2f pointer{0.0f, 0.0f};
    float elapsed = 0.0f;
    bool entered = false;

    Entity* entity(const std::string& id) {
        const auto it = by_id.find(id);
        return it == by_id.end() ? nullptr : &entities[it->second];
    }
    const Entity* entity(const std::string& id) const {
        const auto it = by_id.find(id);
        return it == by_id.end() ? nullptr : &entities[it->second];
    }

    static void apply_transform(Entity& entity) {
        std::visit(
            [&](auto& visual) {
                visual.setPosition(entity.transform.position.x, entity.transform.position.y);
                visual.setScale(entity.transform.scale.x, entity.transform.scale.y);
                visual.setRotation(entity.transform.rotation);
            },
            entity.visual);
    }

    static sf::FloatRect bounds(const Entity& entity) {
        return std::visit(
            [](const auto& visual) -> sf::FloatRect {
                using Visual = std::decay_t<decltype(visual)>;
                if constexpr (std::is_same_v<Visual, sf::Sprite>) {
                    return visual.getGlobalBounds();
                } else {
                    return visual.global_bounds();
                }
            },
            entity.visual);
    }

    template <typename... Args>
    bool call(pac::core::EngineContext& ctx, const char* name, Args&&... args) {
        if (!behavior_valid || disabled_callbacks.contains(name)) {
            return false;
        }
        sol::object value = behavior[name];
        if (!value.valid() || value == sol::lua_nil) {
            return false;
        }
        if (!value.is<sol::protected_function>()) {
            ctx.log.error(std::string("ScriptScene: '") + name + "' must be a function");
            disabled_callbacks.insert(name);
            return false;
        }
        const pac::core::ScopeId previous = ctx.scripting.current_scope();
        ctx.scripting.set_current_scope(scope);
        sol::protected_function fn = value.as<sol::protected_function>();
        const sol::protected_function_result result = fn(std::forward<Args>(args)...);
        ctx.scripting.set_current_scope(previous);
        if (!result.valid()) {
            const sol::error error = result;
            ctx.log.error(std::string("ScriptScene '") + data.id + "' " + name +
                          " error: " + error.what());
            disabled_callbacks.insert(name);
            return false;
        }
        return true;
    }
};

ScriptScene::ScriptScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx), scene_id_(params.get_or("__scene_id", "")), data_path_(params.get_or("data", "")),
      logic_path_(params.get_or("logic", "")), api_state_(std::make_shared<ScriptSceneApiState>()),
      impl_(std::make_unique<Impl>()) {
    api_state_->scene = this;
}

ScriptScene::~ScriptScene() {
    api_state_->scene = nullptr;
}

void ScriptScene::enter() {
    if (data_path_.empty() || logic_path_.empty()) {
        ctx_.log.error("ScriptScene '" + scene_id_ + "' requires 'data' and 'logic'");
        return;
    }
    try {
        impl_->data =
            parse_script_scene(ctx_.resources.read_text(data_path_), scene_id_, data_path_);
        opaque_ = impl_->data.opaque;
        if (!impl_->data.background_image.empty()) {
            impl_->background.emplace(ctx_.resources.texture(impl_->data.background_image));
        }
        impl_->entities.reserve(impl_->data.entities.size());
        for (const ScriptSceneEntityData& source : impl_->data.entities) {
            Impl::Entity entity;
            entity.id = source.id;
            entity.transform = source.transform;
            entity.z = source.z;
            entity.visible = source.visible;
            if (source.sprite) {
                sf::Sprite sprite(ctx_.resources.texture(source.sprite->image));
                sprite.setOrigin(source.sprite->origin);
                sprite.setColor(source.sprite->tint);
                entity.visual = std::move(sprite);
            } else {
                VisualSprite visual = load_visual_sprite(ctx_.resources, source.animation->source);
                if (!visual.has(source.animation->sequence)) {
                    throw AssetError("script-scene-runtime",
                                     "script-scene.animation-sequence-unknown",
                                     "entity '" + source.id + "' has no animation sequence '" +
                                         source.animation->sequence + "'");
                }
                visual.play(source.animation->sequence);
                entity.visual = std::move(visual);
                entity.animated = true;
            }
            Impl::apply_transform(entity);
            impl_->by_id.emplace(entity.id, impl_->entities.size());
            impl_->entities.push_back(std::move(entity));
        }
    } catch (pac::core::LoadError& error) {
        ctx_.log.error(std::string("ScriptScene: ") + error.with_file(data_path_).what());
        return;
    } catch (const std::exception& error) {
        ctx_.log.error(std::string("ScriptScene '") + scene_id_ + "': " + error.what());
        return;
    }

    bind_script_scene_types(ctx_.scripting.lua());
    try {
        sol::state& lua = ctx_.scripting.lua();
        sol::load_result chunk = lua.load(ctx_.resources.read_text(logic_path_), "@" + logic_path_);
        if (!chunk.valid()) {
            const sol::error error = chunk;
            ctx_.log.error(std::string("ScriptScene logic load error: ") + error.what());
            return;
        }
        const sol::protected_function_result result = sol::protected_function(chunk)();
        if (!result.valid()) {
            const sol::error error = result;
            ctx_.log.error(std::string("ScriptScene logic error: ") + error.what());
            return;
        }
        sol::optional<sol::table> behavior = result;
        if (!behavior) {
            ctx_.log.error("ScriptScene logic '" + logic_path_ + "' did not return a table");
            return;
        }
        impl_->behavior = *behavior;
        impl_->behavior_valid = true;
    } catch (const std::exception& error) {
        ctx_.log.error(std::string("ScriptScene logic '") + logic_path_ + "': " + error.what());
        return;
    }

    impl_->scope = ctx_.scripting.open_scope();
    impl_->entered = true;
    impl_->call(ctx_, "on_enter", ScriptSceneContext{api_state_});
}

void ScriptScene::leave() {
    if (impl_->entered) {
        impl_->call(ctx_, "on_leave", ScriptSceneContext{api_state_});
        ctx_.scripting.cancel_scope(impl_->scope);
        impl_->entered = false;
    }
    impl_->keys_down.clear();
    impl_->pointer_buttons_down.clear();
}

void ScriptScene::handle_event(const sf::Event& event) {
    if (!impl_->entered) {
        return;
    }
    sol::state& lua = ctx_.scripting.lua();
    sol::table input = lua.create_table();
    bool recognized = true;
    switch (event.type) {
    case sf::Event::KeyPressed: {
        const std::string key = key_name(event.key.code);
        if (key != "unknown")
            impl_->keys_down.insert(key);
        input["type"] = "key_down";
        input["key"] = key;
        input["alt"] = event.key.alt;
        input["control"] = event.key.control;
        input["shift"] = event.key.shift;
        input["system"] = event.key.system;
        break;
    }
    case sf::Event::KeyReleased: {
        const std::string key = key_name(event.key.code);
        impl_->keys_down.erase(key);
        input["type"] = "key_up";
        input["key"] = key;
        input["alt"] = event.key.alt;
        input["control"] = event.key.control;
        input["shift"] = event.key.shift;
        input["system"] = event.key.system;
        break;
    }
    case sf::Event::MouseMoved:
        impl_->pointer = {static_cast<float>(event.mouseMove.x),
                          static_cast<float>(event.mouseMove.y)};
        input["type"] = "pointer_move";
        input["x"] = impl_->pointer.x;
        input["y"] = impl_->pointer.y;
        break;
    case sf::Event::MouseButtonPressed: {
        impl_->pointer = {static_cast<float>(event.mouseButton.x),
                          static_cast<float>(event.mouseButton.y)};
        const std::string button = button_name(event.mouseButton.button);
        impl_->pointer_buttons_down.insert(button);
        input["type"] = "pointer_down";
        input["button"] = button;
        input["x"] = impl_->pointer.x;
        input["y"] = impl_->pointer.y;
        break;
    }
    case sf::Event::MouseButtonReleased: {
        impl_->pointer = {static_cast<float>(event.mouseButton.x),
                          static_cast<float>(event.mouseButton.y)};
        const std::string button = button_name(event.mouseButton.button);
        impl_->pointer_buttons_down.erase(button);
        input["type"] = "pointer_up";
        input["button"] = button;
        input["x"] = impl_->pointer.x;
        input["y"] = impl_->pointer.y;
        break;
    }
    case sf::Event::TextEntered: {
        const sf::String text(event.text.unicode);
        const auto bytes = text.toUtf8();
        input["type"] = "text_input";
        input["text"] = std::string(bytes.begin(), bytes.end());
        break;
    }
    default:
        recognized = false;
        break;
    }
    if (recognized) {
        impl_->call(ctx_, "on_input", ScriptSceneContext{api_state_}, input);
    }
}

void ScriptScene::update(float dt) {
    if (!impl_->entered) {
        return;
    }
    impl_->call(ctx_, "update", ScriptSceneContext{api_state_}, dt);
    impl_->elapsed += dt;
    for (Impl::Entity& entity : impl_->entities) {
        if (entity.animated) {
            std::get<VisualSprite>(entity.visual).update(dt);
        }
    }
}

void ScriptScene::draw(sf::RenderTarget& target) const {
    const sf::Vector2u resolution = ctx_.display.virtual_resolution();
    sf::RectangleShape fill({static_cast<float>(resolution.x), static_cast<float>(resolution.y)});
    fill.setFillColor(impl_->data.background_color);
    target.draw(fill);
    if (impl_->background) {
        target.draw(*impl_->background);
    }

    std::vector<const Impl::Entity*> ordered;
    ordered.reserve(impl_->entities.size());
    for (const Impl::Entity& entity : impl_->entities) {
        if (entity.visible)
            ordered.push_back(&entity);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
        return a->z < b->z;
    });
    for (const Impl::Entity* entity : ordered) {
        if (entity->animated) {
            std::get<VisualSprite>(entity->visual)
                .draw(target, ctx_.resources, impl_->elapsed, nullptr);
        } else {
            target.draw(std::get<sf::Sprite>(entity->visual));
        }
    }
}

bool ScriptScene::api_entity_exists(const std::string& id) const {
    return impl_->entity(id) != nullptr;
}

namespace {
template <typename T>
std::optional<T> missing_entity(pac::core::Diagnostics& log, const std::string& id) {
    log.error("ScriptScene: unknown entity '" + id + "'");
    return std::nullopt;
}
} // namespace

std::optional<sf::Vector2f> ScriptScene::api_position(const std::string& id) const {
    const Impl::Entity* entity = impl_->entity(id);
    return entity ? std::optional(entity->transform.position)
                  : missing_entity<sf::Vector2f>(ctx_.log, id);
}
std::optional<sf::Vector2f> ScriptScene::api_scale(const std::string& id) const {
    const Impl::Entity* entity = impl_->entity(id);
    return entity ? std::optional(entity->transform.scale)
                  : missing_entity<sf::Vector2f>(ctx_.log, id);
}
std::optional<float> ScriptScene::api_rotation(const std::string& id) const {
    const Impl::Entity* entity = impl_->entity(id);
    return entity ? std::optional(entity->transform.rotation) : missing_entity<float>(ctx_.log, id);
}
std::optional<float> ScriptScene::api_z(const std::string& id) const {
    const Impl::Entity* entity = impl_->entity(id);
    return entity ? std::optional(entity->z) : missing_entity<float>(ctx_.log, id);
}
std::optional<bool> ScriptScene::api_visible(const std::string& id) const {
    const Impl::Entity* entity = impl_->entity(id);
    return entity ? std::optional(entity->visible) : missing_entity<bool>(ctx_.log, id);
}
std::optional<sf::FloatRect> ScriptScene::api_bounds(const std::string& id) const {
    const Impl::Entity* entity = impl_->entity(id);
    return entity ? std::optional(Impl::bounds(*entity))
                  : missing_entity<sf::FloatRect>(ctx_.log, id);
}

bool ScriptScene::api_set_position(const std::string& id, sf::Vector2f value) {
    Impl::Entity* entity = impl_->entity(id);
    if (!entity)
        return missing_entity<bool>(ctx_.log, id).value_or(false);
    entity->transform.position = value;
    Impl::apply_transform(*entity);
    return true;
}
bool ScriptScene::api_translate(const std::string& id, sf::Vector2f delta) {
    Impl::Entity* entity = impl_->entity(id);
    if (!entity)
        return missing_entity<bool>(ctx_.log, id).value_or(false);
    entity->transform.position += delta;
    Impl::apply_transform(*entity);
    return true;
}
bool ScriptScene::api_set_scale(const std::string& id, sf::Vector2f value) {
    Impl::Entity* entity = impl_->entity(id);
    if (!entity)
        return missing_entity<bool>(ctx_.log, id).value_or(false);
    entity->transform.scale = value;
    Impl::apply_transform(*entity);
    return true;
}
bool ScriptScene::api_set_rotation(const std::string& id, float degrees) {
    Impl::Entity* entity = impl_->entity(id);
    if (!entity)
        return missing_entity<bool>(ctx_.log, id).value_or(false);
    entity->transform.rotation = degrees;
    Impl::apply_transform(*entity);
    return true;
}
bool ScriptScene::api_set_z(const std::string& id, float z) {
    Impl::Entity* entity = impl_->entity(id);
    if (!entity)
        return missing_entity<bool>(ctx_.log, id).value_or(false);
    entity->z = z;
    return true;
}
bool ScriptScene::api_set_visible(const std::string& id, bool visible) {
    Impl::Entity* entity = impl_->entity(id);
    if (!entity)
        return missing_entity<bool>(ctx_.log, id).value_or(false);
    entity->visible = visible;
    return true;
}
bool ScriptScene::api_play(const std::string& id, const std::string& sequence, bool restart) {
    Impl::Entity* entity = impl_->entity(id);
    if (!entity)
        return missing_entity<bool>(ctx_.log, id).value_or(false);
    if (!entity->animated) {
        ctx_.log.error("ScriptScene: entity '" + id + "' is not animated");
        return false;
    }
    VisualSprite& visual = std::get<VisualSprite>(entity->visual);
    if (!visual.has(sequence)) {
        ctx_.log.error("ScriptScene: entity '" + id + "' has no sequence '" + sequence + "'");
        return false;
    }
    visual.play(sequence, restart);
    return true;
}
std::optional<std::string> ScriptScene::api_sequence(const std::string& id) const {
    const Impl::Entity* entity = impl_->entity(id);
    if (!entity)
        return missing_entity<std::string>(ctx_.log, id);
    if (!entity->animated)
        return std::nullopt;
    return std::get<VisualSprite>(entity->visual).current_sequence();
}
std::optional<bool> ScriptScene::api_finished(const std::string& id) const {
    const Impl::Entity* entity = impl_->entity(id);
    if (!entity)
        return missing_entity<bool>(ctx_.log, id);
    if (!entity->animated)
        return std::nullopt;
    return std::get<VisualSprite>(entity->visual).finished();
}
bool ScriptScene::api_hit_test(const std::string& id, sf::Vector2f point) const {
    const Impl::Entity* entity = impl_->entity(id);
    if (!entity)
        return missing_entity<bool>(ctx_.log, id).value_or(false);
    return entity->visible && Impl::bounds(*entity).contains(point);
}
bool ScriptScene::api_key_down(const std::string& key) const {
    return impl_->keys_down.contains(key);
}
sf::Vector2f ScriptScene::api_pointer_position() const {
    return impl_->pointer;
}
bool ScriptScene::api_pointer_down(const std::string& button) const {
    return impl_->pointer_buttons_down.contains(button);
}
void ScriptScene::api_goto_scene(const std::string& id) {
    ctx_.scenes.goto_scene(id);
}
void ScriptScene::api_push_scene(const std::string& id) {
    ctx_.scenes.push_scene(id);
}
void ScriptScene::api_pop_scene() {
    ctx_.scenes.pop_scene();
}
void ScriptScene::api_quit() {
    ctx_.scenes.quit();
}

void register_script_scene(pac::core::SceneFactory& factory) {
    factory.register_type("ScriptScene",
                          [](pac::core::EngineContext& ctx, const pac::core::SceneParams& params) {
                              return std::make_unique<ScriptScene>(ctx, params);
                          });
}

} // namespace pac::gfx
