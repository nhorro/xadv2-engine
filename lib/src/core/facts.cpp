#include "engine/core/facts.hpp"

#include <yaml-cpp/yaml.h>

namespace pac::core {

FactsRegistry FactsRegistry::parse(const std::string& yaml_text) {
    FactsRegistry reg;
    const YAML::Node root = YAML::Load(yaml_text);
    if (!root || !root.IsMap()) {
        throw FactsError("facts.yaml: root must be a mapping");
    }
    const YAML::Node namespaces = root["namespaces"];
    if (!namespaces) {
        throw FactsError("facts.yaml: missing required `namespaces` mapping");
    }
    if (!namespaces.IsMap()) {
        throw FactsError("facts.yaml: `namespaces` must be a mapping of <ns> -> [<name>, ...]");
    }
    for (const auto& entry : namespaces) {
        const std::string ns = entry.first.as<std::string>();
        if (ns.empty()) {
            throw FactsError("facts.yaml: empty namespace name");
        }
        if (!entry.second.IsSequence()) {
            throw FactsError("facts.yaml: namespace '" + ns + "' must be a sequence of key names");
        }
        reg.namespaces_.insert(ns);
        for (const auto& item : entry.second) {
            const std::string name = item.as<std::string>();
            if (name.empty()) {
                throw FactsError("facts.yaml: empty key name in namespace '" + ns + "'");
            }
            const std::string fqkey = ns + "." + name;
            if (!reg.keys_.insert(fqkey).second) {
                throw FactsError("facts.yaml: duplicate fact '" + fqkey + "'");
            }
        }
    }
    return reg;
}

} // namespace pac::core
