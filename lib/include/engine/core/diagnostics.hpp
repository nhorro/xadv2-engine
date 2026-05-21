#pragma once

#include <string>
#include <string_view>

namespace pac::core {

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

/// Minimal logging facade. Authoring errors should fail loudly in development;
/// loaders throw typed exceptions (e.g. ManifestError) and the harness logs them.
class Diagnostics {
public:
    explicit Diagnostics(LogLevel min_level = LogLevel::INFO);

    void log(LogLevel level, std::string_view msg) const;
    void debug(std::string_view msg) const;
    void info(std::string_view msg) const;
    void warn(std::string_view msg) const;
    void error(std::string_view msg) const;

    void set_min_level(LogLevel level);
    LogLevel min_level() const { return min_level_; }

private:
    LogLevel min_level_;
};

} // namespace pac::core
