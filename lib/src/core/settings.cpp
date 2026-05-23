#include "engine/core/settings.hpp"

#include <algorithm>

namespace pac::core {

namespace {
float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}
} // namespace

void Settings::clamp() {
    audio.music_volume = clamp01(audio.music_volume);
    audio.sfx_volume = clamp01(audio.sfx_volume);
    if (window_width < 320) {
        window_width = 320;
    }
    if (window_height < 240) {
        window_height = 240;
    }
}

} // namespace pac::core
