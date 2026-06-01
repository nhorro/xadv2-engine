#include "engine/pnc/inventory.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/pnc/data_error.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <utility>

namespace pac::pnc {

namespace {

constexpr const char* kSource = "inventory-loader";

[[noreturn]] void inventory_fail(const std::string& code,
                                 const std::string& msg,
                                 const YAML::Node& at = YAML::Node()) {
    pac::core::fail_at<DataError>(kSource, code, msg, at);
}

} // namespace

std::map<std::string, InventoryItem> parse_inventory(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        inventory_fail("inventory.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }

    std::map<std::string, InventoryItem> items;
    const YAML::Node node = root["items"];
    if (!node || !node.IsMap()) {
        inventory_fail("inventory.items-not-map", "'items' must be a mapping", root);
    }
    for (const auto& kv : node) {
        InventoryItem item;
        item.id = kv.first.as<std::string>();
        const YAML::Node def = kv.second;
        item.name = def["name"] ? def["name"].as<std::string>() : item.id;
        item.description =
            def["description"] ? def["description"].as<std::string>() : std::string();
        item.default_verb = def["default_verb"] ? def["default_verb"].as<std::string>() : "look_at";
        item.combinable = def["combinable"] ? def["combinable"].as<bool>() : false;
        item.icon = def["icon"] ? def["icon"].as<std::string>() : std::string();
        item.icon_cell = def["icon_cell"] ? def["icon_cell"].as<int>() : -1;
        for (const YAML::Node& a : def["affordances"] ? def["affordances"] : YAML::Node()) {
            item.affordances.push_back(a.as<std::string>());
        }
        items.emplace(item.id, std::move(item));
    }
    return items;
}

InventoryIconSheet parse_inventory_icons(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        inventory_fail("inventory.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }

    InventoryIconSheet icons; // defaults to development mode
    const YAML::Node node = root["icons"];
    if (!node) {
        return icons;
    }
    if (!node.IsMap()) {
        inventory_fail("inventory.icons-not-map", "'icons' must be a mapping", node);
    }
    const std::string mode = node["mode"] ? node["mode"].as<std::string>() : "development";
    if (mode == "production") {
        icons.production = true;
    } else if (mode != "development") {
        inventory_fail("inventory.icons-mode-invalid",
                       "icons.mode must be 'development' or 'production'",
                       node["mode"]);
    }
    icons.sheet = node["sheet"] ? node["sheet"].as<std::string>() : std::string();
    icons.columns = node["columns"] ? node["columns"].as<int>() : 1;
    icons.rows = node["rows"] ? node["rows"].as<int>() : 1;

    if (icons.production) {
        if (icons.sheet.empty()) {
            inventory_fail("inventory.icons-sheet-missing",
                           "production icon mode requires a 'sheet' image path",
                           node);
        }
        if (icons.columns <= 0 || icons.rows <= 0) {
            inventory_fail("inventory.icons-grid-invalid",
                           "icons.columns and icons.rows must be positive",
                           node);
        }
    }
    return icons;
}

const InventoryItem* InventoryModel::item(const std::string& id) const {
    const auto it = defs_.find(id);
    return it != defs_.end() ? &it->second : nullptr;
}

void InventoryModel::add(const std::string& id) {
    if (!has(id)) {
        held_.push_back(id);
    }
}

void InventoryModel::remove(const std::string& id) {
    held_.erase(std::remove(held_.begin(), held_.end(), id), held_.end());
}

bool InventoryModel::has(const std::string& id) const {
    return std::find(held_.begin(), held_.end(), id) != held_.end();
}

} // namespace pac::pnc
