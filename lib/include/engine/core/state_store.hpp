#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <variant>

namespace pac::core {

/// Persistent state values are scalars only (bool / number / string) for the MVP,
/// so serialization stays total. (See the scripting API state rules.)
using StateValue = std::variant<bool, double, std::string>;

/// A flat key→scalar store. Backs the global state API (`get_state`/`set_state`);
/// it becomes part of `GameState` in M5. Keys use dotted names by convention.
class StateStore {
public:
    void set(const std::string& key, StateValue value);
    std::optional<StateValue> get(const std::string& key) const;
    bool has(const std::string& key) const;
    void clear();
    std::size_t size() const { return values_.size(); }

private:
    std::map<std::string, StateValue> values_;
};

} // namespace pac::core
