#include "engine/core/profiler.hpp"

#include "engine/core/diagnostics.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace pac::core {

namespace {

// A frame slower than this counts as a dropped frame at 60 Hz (the 16.67 ms
// budget plus headroom for ordinary vsync jitter).
constexpr double kSlowFrameMs = 20.0;

constexpr double kBytesPerMiB = 1024.0 * 1024.0;

std::string mib(std::size_t bytes) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << (static_cast<double>(bytes) / kBytesPerMiB)
       << " MiB";
    return ss.str();
}

std::string one_dp(double v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << v;
    return ss.str();
}

void merge_peak(ResourceStats& into, const ResourceStats& s) {
    into.texture_count = std::max(into.texture_count, s.texture_count);
    into.texture_bytes = std::max(into.texture_bytes, s.texture_bytes);
    into.font_count = std::max(into.font_count, s.font_count);
    into.sound_count = std::max(into.sound_count, s.sound_count);
    into.sound_bytes = std::max(into.sound_bytes, s.sound_bytes);
    into.shader_count = std::max(into.shader_count, s.shader_count);
}

} // namespace

std::size_t process_rss_bytes() {
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    if (!statm) {
        return 0;
    }
    long total_pages = 0;
    long resident_pages = 0;
    if (!(statm >> total_pages >> resident_pages)) {
        return 0;
    }
    const long page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(resident_pages) * static_cast<std::size_t>(page_size);
#else
    return 0;
#endif
}

Profiler::Profiler(Diagnostics& log,
                   std::filesystem::path report_path,
                   std::string game_id,
                   std::function<ResourceStats()> sample_resources,
                   double interval_seconds)
    : log_(log), report_path_(std::move(report_path)), game_id_(std::move(game_id)),
      sample_resources_(std::move(sample_resources)),
      interval_seconds_(interval_seconds > 0.0 ? interval_seconds : 2.0) {
    log_.info("profiling: enabled (report -> " + report_path_.string() + ")");
}

void Profiler::frame(double frame_seconds, double work_seconds, const std::string& scene_id) {
    const double frame_ms = frame_seconds * 1000.0;
    const double work_ms = work_seconds * 1000.0;

    SceneProfile& sp = scenes_[scene_id];
    ++sp.frames;
    sp.frame_ms_sum += frame_ms;
    sp.work_ms_sum += work_ms;
    sp.frame_ms_worst = std::max(sp.frame_ms_worst, frame_ms);
    if (frame_ms > kSlowFrameMs) {
        ++sp.slow_frames;
    }

    ++total_frames_;
    total_seconds_ += frame_seconds;
    worst_frame_ms_ = std::max(worst_frame_ms_, frame_ms);

    interval_seconds_accum_ += frame_seconds;
    ++interval_frames_;
    interval_work_ms_sum_ += work_ms;

    if (interval_seconds_accum_ >= interval_seconds_) {
        take_sample(scene_id);
    }
}

void Profiler::take_sample(const std::string& scene_id) {
    const ResourceStats res = sample_resources_ ? sample_resources_() : ResourceStats{};
    const std::size_t rss = process_rss_bytes();
    peak_rss_bytes_ = std::max(peak_rss_bytes_, rss);
    merge_peak(scenes_[scene_id].peak, res);

    const double fps =
        interval_seconds_accum_ > 0.0 ? interval_frames_ / interval_seconds_accum_ : 0.0;
    const double avg_work_ms =
        interval_frames_ > 0 ? interval_work_ms_sum_ / static_cast<double>(interval_frames_) : 0.0;

    std::ostringstream line;
    line << "profiling: t=" << one_dp(total_seconds_) << "s scene=" << scene_id
         << " fps=" << one_dp(fps) << " work=" << one_dp(avg_work_ms) << "ms"
         << " RAM=" << mib(rss) << " texVRAM=" << mib(res.texture_bytes) << "(" << res.texture_count
         << ") shaders=" << res.shader_count;
    log_.info(line.str());

    interval_seconds_accum_ = 0.0;
    interval_frames_ = 0;
    interval_work_ms_sum_ = 0.0;
}

std::string Profiler::build_report() const {
    // Roll the per-scene profiles up into whole-run totals + global resource peaks.
    std::uint64_t frames = 0;
    double frame_ms_sum = 0.0;
    double work_ms_sum = 0.0;
    std::uint64_t slow_frames = 0;
    ResourceStats peak;
    for (const auto& [id, sp] : scenes_) {
        frames += sp.frames;
        frame_ms_sum += sp.frame_ms_sum;
        work_ms_sum += sp.work_ms_sum;
        slow_frames += sp.slow_frames;
        merge_peak(peak, sp.peak);
    }

    const double avg_frame_ms = frames > 0 ? frame_ms_sum / static_cast<double>(frames) : 0.0;
    const double avg_work_ms = frames > 0 ? work_ms_sum / static_cast<double>(frames) : 0.0;
    const double avg_fps = total_seconds_ > 0.0 ? frames / total_seconds_ : 0.0;
    const double slow_pct = frames > 0 ? 100.0 * slow_frames / static_cast<double>(frames) : 0.0;

    std::ostringstream r;
    r << "=== xadv2-engine profiling report ===\n";
    r << "game:       " << game_id_ << "\n";
    r << "duration:   " << one_dp(total_seconds_) << " s over " << frames << " frames\n";
    r << "frame time: avg " << one_dp(avg_frame_ms) << " ms (" << one_dp(avg_fps) << " fps), worst "
      << one_dp(worst_frame_ms_) << " ms\n";
    r << "work time:  avg " << one_dp(avg_work_ms) << " ms (update+draw, excl. vsync)\n";
    r << "slow frames: " << slow_frames << " of " << frames << " (" << one_dp(slow_pct) << "% > "
      << one_dp(kSlowFrameMs) << " ms)\n";
    r << "peak RAM (RSS):     " << mib(peak_rss_bytes_) << "\n";
    r << "peak texture VRAM:  " << mib(peak.texture_bytes) << " across " << peak.texture_count
      << " textures (upper bound)\n";
    r << "peak shaders:       " << peak.shader_count << "\n";
    r << "peak sounds:        " << peak.sound_count << " (" << mib(peak.sound_bytes) << ")\n";
    r << "peak fonts:         " << peak.font_count << "\n";

    r << "\nper scene:\n";
    for (const auto& [id, sp] : scenes_) {
        const double scene_avg =
            sp.frames > 0 ? sp.frame_ms_sum / static_cast<double>(sp.frames) : 0.0;
        r << "  " << std::left << std::setw(16) << id << std::right << sp.frames << " frames, avg "
          << one_dp(scene_avg) << " ms, worst " << one_dp(sp.frame_ms_worst) << " ms, "
          << sp.slow_frames << " slow, peak VRAM " << mib(sp.peak.texture_bytes) << " ("
          << sp.peak.texture_count << " tex), " << sp.peak.shader_count << " shaders\n";
    }

    r << "\nnote: texture VRAM is an upper-bound estimate (width*height*4, RGBA8); GPU-side\n";
    r << "compression and mipmaps are not accounted for. CPU is approximated by frame time.\n";
    return r.str();
}

void Profiler::finish() {
    if (finished_) {
        return;
    }
    finished_ = true;

    const std::string report = build_report();
    std::ofstream out(report_path_, std::ios::binary | std::ios::trunc);
    if (out) {
        out << report;
        log_.info("profiling: wrote report " + report_path_.string());
    } else {
        log_.warn("profiling: could not write report to " + report_path_.string() +
                  "; dumping to log instead");
        log_.info("\n" + report);
    }
}

} // namespace pac::core
