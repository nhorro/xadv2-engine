#include "engine/pnc/inventory.hpp"

#include "engine/pnc/data_error.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <utility>

namespace pac::pnc {

std::map<std::string, InventoryItem> parse_inventory(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        throw DataError(std::string("inventory: invalid YAML: ") + e.what());
    }

    std::map<std::string, InventoryItem> items;
    const YAML::Node node = root["items"];
    if (!node || !node.IsMap()) {
        throw DataError("inventory: 'items' must be a mapping");
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
        for (const YAML::Node& a : def["affordances"] ? def["affordances"] : YAML::Node()) {
            item.affordances.push_back(a.as<std::string>());
        }
        items.emplace(item.id, std::move(item));
    }
    return items;
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
