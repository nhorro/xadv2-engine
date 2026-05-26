#include "engine/pnc/builtin_scenes.hpp"

#include "engine/core/scene_factory.hpp"
#include "engine/pnc/blank_scene.hpp"
#include "engine/pnc/closeup_scene.hpp"
#include "engine/pnc/cutscene_scene.hpp"
#include "engine/pnc/room_scene.hpp"
#include "engine/pnc/save_load_scene.hpp"
#include "engine/pnc/settings_scene.hpp"
#include "engine/pnc/story_text_scene.hpp"
#include "engine/pnc/title_screen.hpp"

#include <memory>

namespace pac::pnc {

void register_builtin_scenes(pac::core::SceneFactory& factory) {
    factory.register_type("TitleScreen",
                          [](pac::core::EngineContext& ctx, const pac::core::SceneParams& params) {
                              return std::make_unique<TitleScreen>(ctx, params);
                          });
    factory.register_type("SettingsScene",
                          [](pac::core::EngineContext& ctx, const pac::core::SceneParams& params) {
                              return std::make_unique<SettingsScene>(ctx, params);
                          });
    factory.register_type("SaveLoadScene",
                          [](pac::core::EngineContext& ctx, const pac::core::SceneParams& params) {
                              return std::make_unique<SaveLoadScene>(ctx, params);
                          });
    factory.register_type("StoryText",
                          [](pac::core::EngineContext& ctx, const pac::core::SceneParams& params) {
                              return std::make_unique<StoryTextScene>(ctx, params);
                          });
    factory.register_type("Cutscene",
                          [](pac::core::EngineContext& ctx, const pac::core::SceneParams& params) {
                              return std::make_unique<CutsceneScene>(ctx, params);
                          });
    factory.register_type("RoomScene",
                          [](pac::core::EngineContext& ctx, const pac::core::SceneParams& params) {
                              return std::make_unique<RoomScene>(ctx, params);
                          });
    factory.register_type("CloseUp",
                          [](pac::core::EngineContext& ctx, const pac::core::SceneParams& params) {
                              return std::make_unique<CloseUpScene>(ctx, params);
                          });
    factory.register_type("Blank",
                          [](pac::core::EngineContext& ctx, const pac::core::SceneParams& params) {
                              return std::make_unique<BlankScene>(ctx, params);
                          });
}

} // namespace pac::pnc
