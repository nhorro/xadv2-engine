#pragma once

#include <stdexcept>
#include <string>

namespace pac::core {

/// Where a loader diagnostic points: the file path and, when the input came from
/// parsed YAML, the 1-based line/column. `line < 0` means the position is unknown
/// (e.g. an error raised before any node was read, or from a headless test that
/// fed raw text without a path).
struct SourceLocation {
    std::string file;
    int line = -1;
    int column = -1;

    bool has_line() const { return line >= 0; }
    bool has_file() const { return !file.empty(); }
};

/// Structured authoring diagnostic thrown by every YAML/asset loader. Carries the
/// `{ source, location, id, message }` envelope from the M6 design review so the
/// harness (and offline authoring tools) can match on a stable error `code`
/// rather than parsing free text.
///
/// `source` is the subsystem tag (`manifest-loader`, `room-loader`, ...).
/// `code` is a short, stable, dotted error id (`room.id-mismatch`).
/// `what()` renders the whole envelope on one line for logs.
class LoadError : public std::runtime_error {
public:
    LoadError(std::string source,
              std::string code,
              std::string message,
              SourceLocation location = {});

    const char* what() const noexcept override { return rendered_.c_str(); }

    const std::string& source() const { return source_; }
    const std::string& code() const { return code_; }
    const std::string& message() const { return message_; }
    const SourceLocation& location() const { return location_; }

    /// Attach the file path at the `load_*` / call-site boundary. The text-only
    /// `parse_*` functions stay headless-friendly (they never see a path), so the
    /// caller that owns the path enriches the diagnostic and rethrows.
    LoadError& with_file(std::string file);

private:
    void rerender();

    std::string source_;
    std::string code_;
    std::string message_;
    SourceLocation location_;
    std::string rendered_;
};

/// Throw a `LoadError`. Thin convenience so loaders read as `fail(...)`.
[[noreturn]] void
fail(std::string source, std::string code, std::string message, SourceLocation location = {});

} // namespace pac::core
