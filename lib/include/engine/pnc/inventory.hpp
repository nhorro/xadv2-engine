#pragma once

#include <map>
#include <string>
#include <vector>

namespace pac::pnc {

/// Static inventory item definition (from `inventory.yaml`). Behavior lives in
/// `inventory.lua`. M3/M4 render items as their localized name; `icon` is design-for.
struct InventoryItem {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> affordances;
    std::string default_verb = "look_at";
    bool combinable = false;
    std::string icon;
};

/// Parse the `items` map from `inventory.yaml`. Throws DataError on malformed input.
std::map<std::string, InventoryItem> parse_inventory(const std::string& yaml_text);

/// Item definitions plus the player's held items (ordered). Persistent across
/// rooms (it is part of GameState in M5).
class InventoryModel {
public:
    void set_definitions(std::map<std::string, InventoryItem> defs) { defs_ = std::move(defs); }
    [[nodiscard]] const InventoryItem* item(const std::string& id) const;

    void add(const std::string& id);
    void remove(const std::string& id);
    [[nodiscard]] bool has(const std::string& id) const;
    [[nodiscard]] const std::vector<std::string>& list() const { return held_; }

private:
    std::map<std::string, InventoryItem> defs_;
    std::vector<std::string> held_;
};

} // namespace pac::pnc
