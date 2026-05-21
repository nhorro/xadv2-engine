#include "engine/core/scene_params.hpp"

#include <utility>

namespace pac::core {

void SceneParams::set(std::string key, std::string value) {
    values_[std::move(key)] = std::move(value);
}

bool SceneParams::has(const std::string& key) const {
    return values_.find(key) != values_.end();
}

std::optional<std::string> SceneParams::get(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string SceneParams::get_or(const std::string& key, const std::string& fallback) const {
    const auto it = values_.find(key);
    return it != values_.end() ? it->second : fallback;
}

} // namespace pac::core
