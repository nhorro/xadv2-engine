#include "engine/core/diagnostics.hpp"

#include <iostream>

namespace pac::core {

namespace {

const char* level_tag(LogLevel level) {
    switch (level) {
    case LogLevel::DEBUG:
        return "[debug]";
    case LogLevel::INFO:
        return "[info ]";
    case LogLevel::WARN:
        return "[warn ]";
    case LogLevel::ERROR:
        return "[error]";
    }
    return "[?????]";
}

} // namespace

Diagnostics::Diagnostics(LogLevel min_level) : min_level_(min_level) {}

void Diagnostics::log(LogLevel level, std::string_view msg) const {
    if (static_cast<int>(level) < static_cast<int>(min_level_)) {
        return;
    }
    std::ostream& out =
        (level == LogLevel::ERROR || level == LogLevel::WARN) ? std::cerr : std::cout;
    out << level_tag(level) << ' ' << msg << '\n';
}

void Diagnostics::debug(std::string_view msg) const {
    log(LogLevel::DEBUG, msg);
}
void Diagnostics::info(std::string_view msg) const {
    log(LogLevel::INFO, msg);
}
void Diagnostics::warn(std::string_view msg) const {
    log(LogLevel::WARN, msg);
}
void Diagnostics::error(std::string_view msg) const {
    log(LogLevel::ERROR, msg);
}

void Diagnostics::set_min_level(LogLevel level) {
    min_level_ = level;
}

} // namespace pac::core
