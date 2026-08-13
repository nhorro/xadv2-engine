#pragma once

namespace pac::core {
class SceneFactory;
}

namespace pac::pnc {

/// Register the built-in genre scene types, including standard menu overlays
/// such as `SettingsScene` and `ConfirmationScene`.
void register_builtin_scenes(pac::core::SceneFactory& factory);

} // namespace pac::pnc
