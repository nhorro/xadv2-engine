#include "engine/core/gameplay_recorder.hpp"

#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace pac::core {

bool GameplayRecorder::start_csv(const std::filesystem::path& path) {
    stop_csv();
    csv_.clear();
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }
    }
    csv_.open(path, std::ios::out | std::ios::trunc);
    if (!csv_) {
        return false;
    }
    csv_path_ = path;
    started_ = Clock::now();
    csv_ << "timestamp;event_type;event_id;event_data\n";
    csv_.flush();
    return true;
}

void GameplayRecorder::stop_csv() {
    if (csv_.is_open()) {
        csv_.flush();
        csv_.close();
    }
    csv_path_.clear();
}

void GameplayRecorder::add_observer(Observer observer) {
    if (observer) {
        observers_.push_back(std::move(observer));
    }
}

void GameplayRecorder::record(std::string event_type,
                              std::string event_id,
                              std::string event_data) {
    if (!active()) {
        return;
    }
    const double elapsed = std::chrono::duration<double>(Clock::now() - started_).count();
    GameplayEvent event{elapsed, std::move(event_type), std::move(event_id), std::move(event_data)};
    if (csv_.is_open()) {
        csv_ << std::fixed << std::setprecision(3) << event.timestamp << ';'
             << csv_field(event.event_type) << ';' << csv_field(event.event_id) << ';'
             << csv_field(event.event_data) << '\n';
        // A recording should remain useful after a crash during a long playtest.
        csv_.flush();
    }
    for (const Observer& observer : observers_) {
        observer(event);
    }
}

std::string GameplayRecorder::csv_field(std::string_view value) {
    if (value.find_first_of(";\"\r\n") == std::string_view::npos) {
        return std::string(value);
    }
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (const char c : value) {
        if (c == '"') {
            out.push_back('"');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string GameplayRecorder::json_string(std::string_view value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
        switch (c) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (c < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned>(c) << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(c);
            }
        }
    }
    out << '"';
    return out.str();
}

std::string GameplayRecorder::json_object(
    std::initializer_list<std::pair<std::string_view, std::string_view>> fields) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, value] : fields) {
        if (!first) {
            out.push_back(',');
        }
        first = false;
        out += json_string(key);
        out.push_back(':');
        out += json_string(value);
    }
    out.push_back('}');
    return out;
}

} // namespace pac::core
