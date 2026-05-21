#pragma once

#include <map>
#include <optional>
#include <string>

namespace pac::core {

/// Type-specific scene parameters from the manifest. M0 stores scalar values as
/// strings; nested parameters (e.g. RoomScene structures) are added when needed.
class SceneParams {
public:
    void set(std::string key, std::string value);

    bool has(const std::string& key) const;
    std::optional<std::string> get(const std::string& key) const;
    std::string get_or(const std::string& key, const std::string& fallback) const;

private:
    std::map<std::string, std::string> values_;
};

} // namespace pac::core
