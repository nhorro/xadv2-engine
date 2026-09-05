#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pac::core {

/// One semantic event in a gameplay recording. Timestamps are elapsed seconds
/// since recording started, using a monotonic clock so wall-clock adjustments
/// cannot reorder a walkthrough.
struct GameplayEvent {
    double timestamp = 0.0;
    std::string event_type;
    std::string event_id;
    std::string event_data;
};

/// Session-wide semantic event bus with an optional semicolon-delimited CSV
/// sink. Producers know nothing about the transport: future automation can
/// subscribe a socket publisher through add_observer() without changing room,
/// dialog, or scene code.
class GameplayRecorder {
public:
    using Observer = std::function<void(const GameplayEvent&)>;

    /// Start (or replace) CSV output and write its header. Parent directories
    /// are created when needed. Returns false if the path cannot be opened.
    bool start_csv(const std::filesystem::path& path);
    void stop_csv();

    [[nodiscard]] bool active() const { return csv_.is_open() || !observers_.empty(); }
    [[nodiscard]] const std::filesystem::path& csv_path() const { return csv_path_; }

    void add_observer(Observer observer);
    void record(std::string event_type, std::string event_id, std::string event_data = "{}");

    /// Helpers for structured event_data without coupling the recorder to a
    /// JSON dependency. Values are emitted as JSON strings with full escaping.
    [[nodiscard]] static std::string
    json_object(std::initializer_list<std::pair<std::string_view, std::string_view>> fields);

private:
    using Clock = std::chrono::steady_clock;

    static std::string csv_field(std::string_view value);
    static std::string json_string(std::string_view value);

    Clock::time_point started_ = Clock::now();
    std::filesystem::path csv_path_;
    std::ofstream csv_;
    std::vector<Observer> observers_;
};

} // namespace pac::core
