#include "engine/core/resource_source.hpp"

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

} // namespace pac::core
