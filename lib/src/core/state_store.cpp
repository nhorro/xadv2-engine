#include "engine/core/state_store.hpp"

#include <utility>

namespace pac::core {

void StateStore::set(const std::string& key, StateValue value) {
    values_[key] = std::move(value);
}

std::optional<StateValue> StateStore::get(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool StateStore::has(const std::string& key) const {
    return values_.find(key) != values_.end();
}

void StateStore::clear() {
    values_.clear();
}

} // namespace pac::core
