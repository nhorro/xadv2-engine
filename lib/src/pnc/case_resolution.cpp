#include "engine/pnc/case_resolution.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/pnc/data_error.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <iterator>
#include <set>

namespace pac::pnc {
namespace {
constexpr const char* kSource = "case-resolution-loader";

[[noreturn]] void
fail(const std::string& code, const std::string& message, const YAML::Node& at = YAML::Node()) {
    pac::core::fail_at<DataError>(kSource, code, message, at);
}

std::vector<std::string> strings(const YAML::Node& node, const std::string& field) {
    std::vector<std::string> result;
    if (!node)
        return result;
    if (!node.IsSequence())
        fail("case." + field + "-not-sequence", "'" + field + "' must be a list", node);
    for (const YAML::Node& value : node)
        result.push_back(value.as<std::string>());
    return result;
}

geom::Polygon polygon(const YAML::Node& node) {
    geom::Polygon result;
    for (const YAML::Node& p : node)
        result.push_back({p["x"].as<float>(), p["y"].as<float>()});
    return result;
}

std::string resolve(const std::string& raw,
                    const std::string& logical_path,
                    const YAML::Node& at,
                    const std::string& field) {
    if (raw.empty())
        return {};
    if (logical_path.empty())
        return raw;
    const std::string path =
        !raw.empty() && raw.front() == '/'
            ? raw.substr(1)
            : pac::core::logical_join(pac::core::logical_dir(logical_path), raw);
    if (!pac::core::is_valid_logical_path(path))
        fail("case." + field + "-path-invalid",
             field + " did not resolve to a valid logical path",
             at);
    return path;
}

bool has_duplicates(const std::vector<std::string>& values) {
    return std::set<std::string>(values.begin(), values.end()).size() != values.size();
}
} // namespace

sf::Color case_term_color(const std::string& tag, std::uint8_t alpha) {
    if (tag.empty())
        return sf::Color(58, 70, 80, alpha);

    static const sf::Color colors[] = {
        {38, 116, 125},
        {145, 101, 32},
        {111, 67, 112},
        {125, 57, 62},
        {91, 99, 48},
        {46, 92, 130},
    };
    std::size_t hash = 5381;
    for (const unsigned char c : tag)
        hash = ((hash << 5U) + hash) ^ c;
    sf::Color result = colors[hash % std::size(colors)];
    result.a = alpha;
    return result;
}

const CaseTerm* CaseTermBank::find(const std::string& id) const {
    const auto it =
        std::find_if(terms.begin(), terms.end(), [&](const CaseTerm& t) { return t.id == id; });
    return it == terms.end() ? nullptr : &*it;
}

const CaseSlot* CaseResolutionData::slot_at(geom::Point p) const {
    for (const CaseSlot& slot : slots)
        if (geom::point_in_polygon(p, slot.area))
            return &slot;
    return nullptr;
}

CaseTermBank parse_case_terms(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        fail("case.terms-invalid-yaml", e.what());
    }
    if (!root || !root.IsMap())
        fail("case.terms-root-not-map", "root must be a mapping");
    const YAML::Node list = root["terms"];
    if (!list || !list.IsSequence())
        fail("case.terms-missing", "'terms' must be a list", root);

    CaseTermBank bank;
    bank.version = root["version"] ? root["version"].as<int>() : 1;
    std::set<std::string> ids;
    for (const YAML::Node& node : list) {
        if (!node["id"] || !node["name"])
            fail("case.term-fields-missing", "each term needs 'id' and 'name'", node);
        CaseTerm term{node["id"].as<std::string>(), node["name"].as<std::string>(), {}};
        if (node["tag"] && node["tags"])
            fail("case.term-tag-conflict", "term must use 'tag', not both 'tag' and 'tags'", node);
        if (node["tag"])
            term.tag = node["tag"].as<std::string>();
        // Give authors migrating the first iteration a precise error instead of
        // silently accepting ambiguous color semantics.
        if (node["tags"])
            fail("case.term-tags-unsupported",
                 "term tags are singular; use 'tag: value'",
                 node["tags"]);
        if (!ids.insert(term.id).second)
            fail("case.term-id-duplicate", "duplicate term id '" + term.id + "'", node);
        bank.terms.push_back(std::move(term));
    }
    return bank;
}

CaseResolutionData parse_case_resolution(const std::string& yaml_text,
                                         const std::string& expected_id,
                                         const std::string& logical_path) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        fail("case.invalid-yaml", e.what());
    }
    if (!root || !root.IsMap())
        fail("case.root-not-map", "root must be a mapping");
    if (!root["id"])
        fail("case.id-missing", "'id' is required", root);
    if (!root["background"])
        fail("case.background-missing", "'background' is required", root);

    CaseResolutionData data;
    data.version = root["version"] ? root["version"].as<int>() : 1;
    data.id = root["id"].as<std::string>();
    if (!expected_id.empty() && expected_id != data.id)
        fail("case.id-mismatch", "id does not match filename", root["id"]);
    data.background = resolve(root["background"].as<std::string>(),
                              logical_path,
                              root["background"],
                              "background");
    if (root["canvas_height"])
        data.canvas_height = root["canvas_height"].as<float>();

    const YAML::Node slots = root["slots"];
    if (!slots || !slots.IsMap())
        fail("case.slots-missing", "'slots' must be a mapping", root);
    for (const auto& kv : slots) {
        CaseSlot slot;
        slot.id = kv.first.as<std::string>();
        const YAML::Node node = kv.second;
        if (!node["area"])
            fail("case.slot-area-missing", "slot '" + slot.id + "' needs an area", node);
        slot.area = polygon(node["area"]);
        if (slot.area.size() < 3)
            fail("case.slot-area-degenerate", "slot area needs at least 3 points", node["area"]);
        slot.accepts = strings(node["accepts"], "accepts");
        if (node["solution"])
            slot.solution = node["solution"].as<std::string>();
        data.slots.push_back(std::move(slot));
    }

    if (const YAML::Node groups = root["solution_groups"]) {
        if (!groups.IsMap())
            fail("case.solution-groups-not-map", "'solution_groups' must be a mapping", groups);
        std::set<std::string> grouped_slots;
        for (const auto& kv : groups) {
            CaseSolutionGroup group;
            group.id = kv.first.as<std::string>();
            const YAML::Node node = kv.second;
            if (!node.IsMap())
                fail("case.solution-group-not-map",
                     "solution group '" + group.id + "' must be a mapping",
                     node);
            group.slots = strings(node["slots"], "solution-group-slots");
            group.terms = strings(node["terms"], "solution-group-terms");
            if (group.slots.empty() || group.terms.empty())
                fail("case.solution-group-empty",
                     "solution group '" + group.id + "' needs non-empty slots and terms",
                     node);
            if (group.slots.size() != group.terms.size())
                fail("case.solution-group-size-mismatch",
                     "solution group '" + group.id + "' needs one term per slot",
                     node);
            if (has_duplicates(group.slots) || has_duplicates(group.terms))
                fail("case.solution-group-duplicates",
                     "solution group '" + group.id + "' cannot repeat slots or terms",
                     node);
            for (const std::string& slot_id : group.slots) {
                const auto slot = std::find_if(
                    data.slots.begin(),
                    data.slots.end(),
                    [&](const CaseSlot& candidate) { return candidate.id == slot_id; });
                if (slot == data.slots.end())
                    fail("case.solution-group-slot-unknown",
                         "solution group '" + group.id + "' names unknown slot '" + slot_id + "'",
                         node["slots"]);
                if (!slot->solution.empty())
                    fail("case.solution-group-slot-conflict",
                         "grouped slot '" + slot_id + "' must not define 'solution'",
                         node["slots"]);
                if (!grouped_slots.insert(slot_id).second)
                    fail("case.solution-group-slot-reused",
                         "slot '" + slot_id + "' belongs to more than one solution group",
                         node["slots"]);
            }
            data.solution_groups.push_back(std::move(group));
        }
    }

    if (const YAML::Node sounds = root["sounds"]) {
        if (!sounds.IsMap())
            fail("case.sounds-not-map", "'sounds' must be a mapping", sounds);
        const auto sound = [&](const char* name) -> std::string {
            const YAML::Node value = sounds[name];
            if (!value)
                return {};
            if (!value.IsScalar())
                fail("case.sound-not-path",
                     std::string("'sounds.") + name + "' must be a resource path",
                     value);
            return resolve(value.as<std::string>(), logical_path, value, "sound");
        };
        data.sounds.pickup = sound("pickup");
        data.sounds.place = sound("place");
        data.sounds.return_to_bank = sound("return");
    }
    return data;
}

bool case_slot_accepts(const CaseSlot& slot, const CaseTerm& term) {
    if (slot.accepts.empty() || term.tag.empty())
        return true;
    return std::find(slot.accepts.begin(), slot.accepts.end(), term.tag) != slot.accepts.end();
}

bool CaseAssignments::assign(const CaseSlot& slot, const CaseTerm& term) {
    if (!case_slot_accepts(slot, term))
        return false;
    for (auto it = by_slot_.begin(); it != by_slot_.end();) {
        if (it->second == term.id)
            it = by_slot_.erase(it);
        else
            ++it;
    }
    by_slot_[slot.id] = term.id;
    return true;
}

void CaseAssignments::clear(const std::string& slot_id) {
    by_slot_.erase(slot_id);
}
const std::string* CaseAssignments::term_for(const std::string& slot_id) const {
    const auto it = by_slot_.find(slot_id);
    return it == by_slot_.end() ? nullptr : &it->second;
}
bool CaseAssignments::complete(const CaseResolutionData& data) const {
    return std::all_of(data.slots.begin(), data.slots.end(), [&](const CaseSlot& slot) {
        return term_for(slot.id) != nullptr;
    });
}
std::size_t CaseAssignments::invalid_count(const CaseResolutionData& data) const {
    std::size_t invalid = 0;
    std::set<std::string> grouped_slots;
    for (const CaseSolutionGroup& group : data.solution_groups) {
        grouped_slots.insert(group.slots.begin(), group.slots.end());
        std::set<std::string> actual;
        for (const std::string& slot_id : group.slots) {
            if (const std::string* value = term_for(slot_id))
                actual.insert(*value);
        }
        for (const std::string& expected : group.terms) {
            if (!actual.contains(expected))
                ++invalid;
        }
    }
    for (const CaseSlot& slot : data.slots) {
        if (grouped_slots.contains(slot.id))
            continue;
        const std::string* value = term_for(slot.id);
        if (!value || (!slot.solution.empty() && *value != slot.solution))
            ++invalid;
    }
    return invalid;
}
bool CaseAssignments::solved(const CaseResolutionData& data) const {
    return complete(data) && invalid_count(data) == 0;
}
} // namespace pac::pnc
