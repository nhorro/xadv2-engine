#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace pac::core {

class ResourceError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// A logical resource path is relative, forward-slash separated, with no `..` /
/// `.` segments, no backslashes, and no drive/scheme (`:`). Game code and scripts
/// use logical paths exclusively; the resource layer resolves them to bytes.
bool is_valid_logical_path(const std::string& logical);

/// Directory portion of a logical path ("a/b/c.png" -> "a/b"; "c.png" -> "").
std::string logical_dir(const std::string& logical);

/// Join a logical directory with a path that may be relative to it. An absolute-
/// looking `rel` (already containing slashes from the root) is returned as-is when
/// `dir` is empty; otherwise `dir/rel`. Used to resolve assets referenced relative
/// to the file that names them (e.g. a spritesheet's image, an anim's spritesheet).
std::string logical_join(const std::string& dir, const std::string& rel);

/// Opens readable assets named by logical path. Backends: filesystem (MVP) and a
/// packed archive (design-for).
class ResourceSource {
public:
    virtual ~ResourceSource() = default;

    virtual bool exists(const std::string& logical) const = 0;
    virtual std::string read_text(const std::string& logical) const = 0;
    virtual std::vector<std::byte> read_bytes(const std::string& logical) const = 0;
};

/// Reads loose files under a filesystem root.
class FilesystemResourceSource : public ResourceSource {
public:
    explicit FilesystemResourceSource(std::string root);

    const std::string& root() const { return root_; }

    bool exists(const std::string& logical) const override;
    std::string read_text(const std::string& logical) const override;
    std::vector<std::byte> read_bytes(const std::string& logical) const override;

    /// Host filesystem path for a logical path. Only for third-party APIs that
    /// must take a path (sf::Font/sf::Music); throws ResourceError on an invalid
    /// logical path.
    std::string host_path(const std::string& logical) const;

private:
    std::string root_;
};

} // namespace pac::core
