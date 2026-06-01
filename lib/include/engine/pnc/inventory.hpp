#pragma once

#include <map>
#include <string>
#include <vector>

namespace pac::pnc {

/// Static inventory item definition (from `inventory.yaml`). Behavior lives in
/// `inventory.lua`. M3/M4 render items as their localized name; the icon fields
/// drive the SCUMM panel's icon slots (issue #172):
///   - `icon`      individual PNG, used in the development icon mode.
///   - `icon_cell` row-major cell index into the shared sheet, used in production.
struct InventoryItem {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> affordances;
    std::string default_verb = "look_at";
    bool combinable = false;
    std::string icon;
    int icon_cell = -1; // -1 = none; index into the production icon sheet
};

/// How inventory icons are sourced (the `icons` block of `inventory.yaml`):
///   - development: one PNG per item (`InventoryItem::icon`); fast to iterate on.
///   - production:  a single MxN sheet texture; each item picks a cell by index.
struct InventoryIconSheet {
    bool production = false; // false = development (per-item images)
    std::string sheet;       // atlas texture path (production mode)
    int columns = 1;         // sheet grid columns
    int rows = 1;            // sheet grid rows
};

/// Parse the `items` map from `inventory.yaml`. Throws DataError on malformed input.
std::map<std::string, InventoryItem> parse_inventory(const std::string& yaml_text);

/// Parse the optional `icons` block from `inventory.yaml` (icon source mode).
/// Returns development mode (no sheet) when the block is absent. Throws DataError
/// when production mode is selected without a valid sheet / grid.
InventoryIconSheet parse_inventory_icons(const std::string& yaml_text);

/// Item definitions plus the player's held items (ordered). Persistent across
/// rooms (it is part of GameState in M5).
class InventoryModel {
public:
    void set_definitions(std::map<std::string, InventoryItem> defs) { defs_ = std::move(defs); }
    [[nodiscard]] const std::map<std::string, InventoryItem>& definitions() const { return defs_; }
    [[nodiscard]] const InventoryItem* item(const std::string& id) const;

    void set_icon_sheet(InventoryIconSheet sheet) { icon_sheet_ = std::move(sheet); }
    [[nodiscard]] const InventoryIconSheet& icon_sheet() const { return icon_sheet_; }

    void add(const std::string& id);
    void remove(const std::string& id);
    [[nodiscard]] bool has(const std::string& id) const;
    [[nodiscard]] const std::vector<std::string>& list() const { return held_; }

    /// Replace held items with the given list (preserves order). Used when
    /// restoring a save.
    void replace_all(std::vector<std::string> ids) { held_ = std::move(ids); }

private:
    std::map<std::string, InventoryItem> defs_;
    InventoryIconSheet icon_sheet_;
    std::vector<std::string> held_;
};

} // namespace pac::pnc
