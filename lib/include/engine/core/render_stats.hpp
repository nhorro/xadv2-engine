#pragma once

#include <cstddef>
#include <cstdint>

namespace pac::core {

/// Process-wide GPU render instrumentation for the profiling mode (#112). The
/// gfx layer (`ShaderChain`) reports into these counters; the app loop / profiler
/// reads them. They live in core (not gfx) so the core profiler can read them
/// without violating the layer dependency rule — gfx depends on core, never the
/// reverse. The engine is single-threaded, so plain counters suffice; the cost is
/// a few integer adds and is negligible even when profiling is off.
struct RenderStats {
    std::uint64_t shader_passes = 0; // ShaderChain passes since the last reset
    std::size_t shader_rt_bytes = 0; // live VRAM of ShaderChain render-target pools
};

/// Add to the per-frame shader-pass counter (called by ShaderChain, once per pass).
void note_shader_passes(std::uint64_t n);

/// Adjust the live shader render-target VRAM total (called by ShaderChain when it
/// grows or frees its render-target pool; `delta` is a signed byte count).
void add_shader_rt_bytes(std::ptrdiff_t delta);

/// Snapshot of the current counters.
RenderStats render_stats();

/// Zero the per-frame pass counter (the app loop calls this each frame after
/// sampling). The live RT byte total is not reset — it tracks allocation, not
/// per-frame work.
void reset_shader_passes();

} // namespace pac::core
