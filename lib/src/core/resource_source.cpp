#include "engine/core/resource_source.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace pac::core {

bool is_valid_logical_path(const std::string& logical) {
    if (logical.empty()) {
        return false;
    }
    if (logical.front() == '/') {
        return false; // absolute
    }
    if (logical.find('\\') != std::string::npos) {
        return false; // backslash (platform path)
    }
    if (logical.find(':') != std::string::npos) {
        return false; // drive letter / scheme
    }
    // Every '/'-separated segment must be a real name (no empty, '.', '..').
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = logical.find('/', start);
        const std::string segment =
            logical.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

std::string logical_dir(const std::string& logical) {
    const std::size_t slash = logical.find_last_of('/');
    return slash == std::string::npos ? std::string() : logical.substr(0, slash);
}

std::string logical_join(const std::string& dir, const std::string& rel) {
    // A leading '/' means "from the resource root", not "relative to the file
    // that named me" — the escape hatch for assets shared between directories
    // (a background under `backgrounds/` referenced from `rooms/study.yaml`).
    // Without it, `dir + "/" + rel` would produce `rooms//backgrounds/...`,
    // which is not a valid logical path. Close-ups already documented this
    // convention; honouring it here gives it to room layers, object sprites and
    // panel art too.
    if (!rel.empty() && rel.front() == '/') {
        return rel.substr(1);
    }
    if (dir.empty()) {
        return rel;
    }
    return dir + "/" + rel;
}

FilesystemResourceSource::FilesystemResourceSource(std::string root) : root_(std::move(root)) {}

std::string FilesystemResourceSource::host_path(const std::string& logical) const {
    if (!is_valid_logical_path(logical)) {
        throw ResourceError("invalid logical path: '" + logical + "'");
    }
    return (std::filesystem::path(root_) / logical).string();
}

bool FilesystemResourceSource::exists(const std::string& logical) const {
    if (!is_valid_logical_path(logical)) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(root_) / logical, ec);
}

std::string FilesystemResourceSource::read_text(const std::string& logical) const {
    const std::string path = host_path(logical);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ResourceError("cannot read resource '" + logical + "' (" + path + ")");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::byte> FilesystemResourceSource::read_bytes(const std::string& logical) const {
    const std::string path = host_path(logical);
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw ResourceError("cannot read resource '" + logical + "' (" + path + ")");
    }
    const std::streamsize size = in.tellg();
    in.seekg(0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(bytes.data()), size);
    }
    return bytes;
}

std::vector<std::string> FilesystemResourceSource::list(const std::string& prefix,
                                                        const std::string& suffix) const {
    std::vector<std::string> out;
    std::error_code ec;
    const std::filesystem::path base =
        prefix.empty() ? std::filesystem::path(root_) : std::filesystem::path(root_) / prefix;
    if (!std::filesystem::is_directory(base, ec)) {
        return out;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(base, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        std::error_code ec2;
        const auto rel = std::filesystem::relative(entry.path(), std::filesystem::path(root_), ec2);
        if (ec2) {
            continue;
        }
        // Force forward slashes so the result is a logical path.
        std::string logical = rel.generic_string();
        if (!suffix.empty() &&
            (logical.size() < suffix.size() ||
             logical.compare(logical.size() - suffix.size(), suffix.size(), suffix) != 0)) {
            continue;
        }
        out.push_back(std::move(logical));
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace pac::core
