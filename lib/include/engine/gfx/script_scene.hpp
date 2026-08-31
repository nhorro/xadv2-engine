#pragma once

#include "engine/core/scene.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pac::core {
class SceneFactory;
class SceneParams;
struct EngineContext;
} // namespace pac::core

namespace pac::gfx {

struct ScriptSceneTransform {
    sf::Vector2f position{0.0f, 0.0f};
    sf::Vector2f scale{1.0f, 1.0f};
    float rotation = 0.0f;
};

struct ScriptSceneSprite {
    std::string image;
    sf::Vector2f origin{0.0f, 0.0f};
    sf::Color tint = sf::Color::White;
};

struct ScriptSceneAnimation {
    std::string source;
    std::string sequence;
};

/// One declarative entity in a ScriptScene. The sections intentionally resemble
/// components even though the MVP runtime is a compact id-indexed registry, not
/// an ECS. Exactly one of `sprite` / `animation` is present.
struct ScriptSceneEntityData {
    std::string id;
    ScriptSceneTransform transform;
    std::optional<ScriptSceneSprite> sprite;
    std::optional<ScriptSceneAnimation> animation;
    float z = 0.0f;
    bool visible = true;
};

struct ScriptSceneData {
    int version = 1;
    std::string id;
    sf::Color background_color = sf::Color::Black;
    std::string background_image;
    bool opaque = true;
    std::vector<ScriptSceneEntityData> entities;
};

/// Parse the generic scripted-scene YAML without loading graphics resources.
/// Asset paths resolve relative to `logical_path`; a leading slash makes them
/// resource-root-relative. Throws AssetError with stable diagnostic codes.
ScriptSceneData parse_script_scene(const std::string& yaml_text,
                                   const std::string& expected_id = {},
                                   const std::string& logical_path = {});

struct ScriptSceneApiState;
struct ScriptSceneContext;
struct ScriptEntityHandle;

/// Generic data + Lua 2D scene. YAML owns initial entities; Lua receives
/// normalized input, a fixed-timestep update, and id-resolving entity handles.
class ScriptScene final : public pac::core::Scene {
public:
    ScriptScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);
    ~ScriptScene() override;

    void enter() override;
    void leave() override;
    void handle_event(const sf::Event& event) override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

private:
    friend struct ScriptSceneContext;
    friend struct ScriptEntityHandle;

    bool api_entity_exists(const std::string& id) const;
    std::optional<sf::Vector2f> api_position(const std::string& id) const;
    std::optional<sf::Vector2f> api_scale(const std::string& id) const;
    std::optional<float> api_rotation(const std::string& id) const;
    std::optional<float> api_z(const std::string& id) const;
    std::optional<bool> api_visible(const std::string& id) const;
    std::optional<sf::FloatRect> api_bounds(const std::string& id) const;
    bool api_set_position(const std::string& id, sf::Vector2f value);
    bool api_translate(const std::string& id, sf::Vector2f delta);
    bool api_set_scale(const std::string& id, sf::Vector2f value);
    bool api_set_rotation(const std::string& id, float degrees);
    bool api_set_z(const std::string& id, float z);
    bool api_set_visible(const std::string& id, bool visible);
    bool api_play(const std::string& id, const std::string& sequence, bool restart);
    std::optional<std::string> api_sequence(const std::string& id) const;
    std::optional<bool> api_finished(const std::string& id) const;
    bool api_hit_test(const std::string& id, sf::Vector2f point) const;
    bool api_key_down(const std::string& key) const;
    sf::Vector2f api_pointer_position() const;
    bool api_pointer_down(const std::string& button) const;
    void api_goto_scene(const std::string& id);
    void api_push_scene(const std::string& id);
    void api_pop_scene();
    void api_quit();

    pac::core::EngineContext& ctx_;
    std::string scene_id_;
    std::string data_path_;
    std::string logic_path_;
    std::shared_ptr<ScriptSceneApiState> api_state_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Register `type: ScriptScene` with a scene factory. The point-and-click built-in
/// registrar calls this too, so existing standard game launchers gain it.
void register_script_scene(pac::core::SceneFactory& factory);

} // namespace pac::gfx
