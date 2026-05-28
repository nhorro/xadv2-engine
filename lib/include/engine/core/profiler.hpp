#pragma once

#include "engine/core/resource_stats.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace pac::core {

class Diagnostics;

/// Resident set size of this process in bytes, or 0 when unavailable (non-Linux,
/// or /proc unreadable). A cheap RAM proxy for the profiling mode; safe anywhere.
std::size_t process_rss_bytes();

/// Development-only resource profiler (#112), gated by `development.profiling` in
/// the manifest. It is the cheapest honest answer to "will this game fit a
/// Thimbleweed/Monkey-Island-class target": frame pacing approximates CPU cost,
/// and the resource cache's live texture footprint approximates VRAM.
///
/// Usage: call `frame()` once per rendered frame; it accumulates timing every
/// frame (cheap) and only samples RAM + resources (via the supplied callback)
/// once per `interval_seconds`, logging a one-line summary each time. Call
/// `finish()` at shutdown to flush an aggregate report. Headless-testable: the
/// resource sampler is a callback and RAM sampling degrades to 0, so no window
/// or GPU context is required.
class Profiler {
public:
    /// Per-scene rollup. Timing is summed every frame; resource peaks are the
    /// element-wise maximum across the interval samples taken while the scene
    /// was active.
    struct SceneProfile {
        std::uint64_t frames = 0;
        double frame_ms_sum = 0.0; // wall-clock frame time (includes vsync wait)
        double work_ms_sum = 0.0;  // update+draw time (excludes the vsync wait)
        double frame_ms_worst = 0.0;
        std::uint64_t slow_frames = 0; // frames slower than the slow-frame threshold
        ResourceStats peak;
    };

    Profiler(Diagnostics& log,
             std::filesystem::path report_path,
             std::string game_id,
             std::function<ResourceStats()> sample_resources,
             double interval_seconds = 2.0);

    /// One rendered frame. `frame_seconds` is the full frame-to-frame wall time;
    /// `work_seconds` is the update+draw cost before the vsync wait; `scene_id`
    /// labels the active top-level scene.
    void frame(double frame_seconds, double work_seconds, const std::string& scene_id);

    /// Write the aggregate report to the report path and log a final summary.
    /// Idempotent — a second call is a no-op.
    void finish();

    /// Test/inspection accessors.
    const std::map<std::string, SceneProfile>& scenes() const { return scenes_; }
    std::size_t peak_rss_bytes() const { return peak_rss_bytes_; }
    std::uint64_t total_frames() const { return total_frames_; }
    double total_seconds() const { return total_seconds_; }

private:
    void take_sample(const std::string& scene_id);
    std::string build_report() const;

    Diagnostics& log_;
    std::filesystem::path report_path_;
    std::string game_id_;
    std::function<ResourceStats()> sample_resources_;
    double interval_seconds_;

    // Whole-run aggregates.
    std::map<std::string, SceneProfile> scenes_;
    std::uint64_t total_frames_ = 0;
    double total_seconds_ = 0.0;
    double worst_frame_ms_ = 0.0;
    std::size_t peak_rss_bytes_ = 0;

    // Current sampling interval accumulators (reset on each sample).
    double interval_seconds_accum_ = 0.0;
    std::uint64_t interval_frames_ = 0;
    double interval_work_ms_sum_ = 0.0;

    bool finished_ = false;
};

} // namespace pac::core
