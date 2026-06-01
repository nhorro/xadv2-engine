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

bool StateStore::erase(const std::string& key) {
    return values_.erase(key) != 0;
}

std::size_t StateStore::erase_prefix(const std::string& prefix) {
    std::size_t erased = 0;
    for (auto it = values_.lower_bound(prefix); it != values_.end() && it->first.rfind(prefix, 0) == 0;) {
        it = values_.erase(it);
        ++erased;
    }
    return erased;
}

void StateStore::clear() {
    values_.clear();
}

} // namespace pac::core
