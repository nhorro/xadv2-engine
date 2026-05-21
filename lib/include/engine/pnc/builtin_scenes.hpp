#pragma once

namespace pac::core {
class SceneFactory;
}

namespace pac::pnc {

/// Register the built-in genre scene types (`TitleScreen`, `SettingsScene`,
/// `Blank`) into a factory. `StoryText` and `RoomScene` are added in M2/M3.
void register_builtin_scenes(pac::core::SceneFactory& factory);

} // namespace pac::pnc
