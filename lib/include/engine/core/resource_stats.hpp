#pragma once

#include <cstddef>

namespace pac::core {

/// A snapshot of what the resource cache currently holds, used by the profiling
/// mode (#112) to track memory growth during development. `texture_bytes` is an
/// upper-bound VRAM estimate (width*height*4, RGBA8) — GPU-side compression and
/// mipmaps are not accounted for. Kept dependency-free so both the cache header
/// (heavy SFML includes) and the profiler can share it.
struct ResourceStats {
    std::size_t texture_count = 0;
    std::size_t texture_bytes = 0; // estimated VRAM: sum of width*height*4
    std::size_t font_count = 0;
    std::size_t sound_count = 0;
    std::size_t sound_bytes = 0; // decoded PCM: sample_count * 2 (16-bit)
    std::size_t shader_count = 0;
};

} // namespace pac::core
