#include "engine/core/load_error.hpp"

#include <utility>

namespace pac::core {

namespace {

std::string render(const std::string& source,
                   const std::string& code,
                   const std::string& message,
                   const SourceLocation& loc) {
    std::string out = "[" + source + "] ";
    if (loc.has_file()) {
        out += loc.file;
        if (loc.has_line()) {
            out += ":" + std::to_string(loc.line);
            if (loc.column >= 0) {
                out += ":" + std::to_string(loc.column);
            }
        }
        out += ' ';
    } else if (loc.has_line()) {
        out += "line " + std::to_string(loc.line) + ' ';
    }
    if (!code.empty()) {
        out += "(" + code + ") ";
    }
    out += message;
    return out;
}

} // namespace

LoadError::LoadError(std::string source,
                     std::string code,
                     std::string message,
                     SourceLocation location)
    : std::runtime_error(""), source_(std::move(source)), code_(std::move(code)),
      message_(std::move(message)), location_(std::move(location)) {
    rerender();
}

void LoadError::rerender() {
    rendered_ = render(source_, code_, message_, location_);
}

LoadError& LoadError::with_file(std::string file) {
    location_.file = std::move(file);
    rerender();
    return *this;
}

void fail(std::string source, std::string code, std::string message, SourceLocation location) {
    throw LoadError(std::move(source), std::move(code), std::move(message), std::move(location));
}

} // namespace pac::core
