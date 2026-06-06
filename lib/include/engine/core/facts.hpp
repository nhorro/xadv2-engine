#pragma once

#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace pac::core {

/// Thrown by FactsRegistry::parse on a malformed `facts.yaml`.
class FactsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// The declared inventory of global state-flag keys (issue #188).
///
/// A game may declare its facts in `facts.yaml`
/// (`namespaces: { <ns>: [<name>, ...] }`); the `facts.<ns>.<name>` Lua proxy
/// then warns — in development builds — when a key outside this set is read or
/// written, catching the typo that would otherwise make a condition silently
/// false forever. An empty registry (no `facts.yaml`) leaves the proxy working
/// as plain `get_state`/`set_state` sugar with the guard disabled, so a game
/// opts in simply by shipping the file.
class FactsRegistry {
public:
    FactsRegistry() = default;

    /// Parse a `facts.yaml` document. Throws FactsError on a structural problem.
    static FactsRegistry parse(const std::string& yaml_text);

    [[nodiscard]] bool empty() const { return keys_.empty(); }
    [[nodiscard]] bool has_namespace(const std::string& ns) const {
        return namespaces_.count(ns) > 0;
    }
    /// `fqkey` is the fully-qualified `"<ns>.<name>"`.
    [[nodiscard]] bool is_declared(const std::string& fqkey) const {
        return keys_.count(fqkey) > 0;
    }

    [[nodiscard]] const std::set<std::string>& namespaces() const { return namespaces_; }
    [[nodiscard]] const std::unordered_set<std::string>& keys() const { return keys_; }

private:
    std::set<std::string> namespaces_;
    std::unordered_set<std::string> keys_; // fully-qualified "<ns>.<name>"
};

} // namespace pac::core
