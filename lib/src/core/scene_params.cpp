#include "engine/core/scene_params.hpp"

#include <set>
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

std::vector<std::string> SceneParams::children(const std::string& prefix) const {
    const std::string dotted = prefix.empty() ? std::string() : prefix + ".";
    std::set<std::string> names;
    for (const auto& [key, value] : values_) {
        (void)value;
        if (key.rfind(dotted, 0) != 0 || key.size() == dotted.size()) {
            continue;
        }
        const std::string rest = key.substr(dotted.size());
        const std::size_t end = rest.find('.');
        names.insert(rest.substr(0, end));
    }
    return {names.begin(), names.end()};
}

} // namespace pac::core
