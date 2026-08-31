#pragma once

#include <filesystem>

namespace sf {
class RenderWindow;
}

namespace pac::core {

/// Save the window's current framebuffer as a PNG (or another format selected
/// by `path`'s extension). Call after drawing and before RenderWindow::display().
/// Returns false when the framebuffer cannot be copied or the file cannot be
/// written. The caller owns directory creation and diagnostics.
[[nodiscard]] bool save_screenshot(const sf::RenderWindow& window,
                                   const std::filesystem::path& path);

} // namespace pac::core
