#include "engine/core/render_stats.hpp"

namespace pac::core {

namespace {
// Single-threaded engine; these are plain process-wide accumulators. `g_rt_bytes`
// is signed so the grow/free deltas from ShaderChain can never underflow the
// type — it is clamped to >= 0 on read.
std::uint64_t g_shader_passes = 0;
std::ptrdiff_t g_shader_rt_bytes = 0;
} // namespace

void note_shader_passes(std::uint64_t n) {
    g_shader_passes += n;
}

void add_shader_rt_bytes(std::ptrdiff_t delta) {
    g_shader_rt_bytes += delta;
}

RenderStats render_stats() {
    RenderStats s;
    s.shader_passes = g_shader_passes;
    s.shader_rt_bytes = g_shader_rt_bytes > 0 ? static_cast<std::size_t>(g_shader_rt_bytes) : 0;
    return s;
}

void reset_shader_passes() {
    g_shader_passes = 0;
}

} // namespace pac::core
