#include "engine/pnc/closeup.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/pnc/data_error.hpp"

#include <yaml-cpp/yaml.h>

#include <utility>

namespace pac::pnc {

namespace {

constexpr const char* kSource = "closeup-loader";

[[noreturn]] void
closeup_fail(const std::string& code, const std::string& msg, const YAML::Node& at = YAML::Node()) {
    pac::core::fail_at<DataError>(kSource, code, msg, at);
}

geom::Point parse_point(const YAML::Node& node) {
    return {node["x"].as<float>(), node["y"].as<float>()};
}

// Resolve a `background` value against the close-up YAML's location:
//   - empty logical_path  -> use raw (headless tests)
//   - leading '/'         -> resources-root-relative (drop the slash)
//   - otherwise           -> relative to the YAML's directory
// The result is validated as a logical path.
std::string
resolve_background(const std::string& raw, const std::string& logical_path, const YAML::Node& at) {
    if (logical_path.empty()) {
        return raw;
    }
    std::string resolved;
    if (!raw.empty() && raw.front() == '/') {
        resolved = raw.substr(1);
    } else {
        resolved = pac::core::logical_join(pac::core::logical_dir(logical_path), raw);
    }
    if (!pac::core::is_valid_logical_path(resolved)) {
        closeup_fail("closeup.background-path-invalid",
                     "'background: " + raw + "' did not resolve to a valid logical path",
                     at);
    }
    return resolved;
}

geom::Polygon parse_polygon(const YAML::Node& node) {
    geom::Polygon poly;
    for (const YAML::Node& p : node) {
        poly.push_back(parse_point(p));
    }
    return poly;
}

} // namespace

const CloseUpHotspot* CloseUpData::hotspot_at(geom::Point p) const {
    for (const CloseUpHotspot& hs : hotspots) {
        if (geom::point_in_polygon(p, hs.area)) {
            return &hs;
        }
    }
    return nullptr;
}

CloseUpData parse_closeup(const std::string& yaml_text,
                          const std::string& expected_id,
                          const std::string& logical_path,
                          sf::Color default_background_color) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        closeup_fail("closeup.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }
    if (!root || !root.IsMap()) {
        closeup_fail("closeup.root-not-map", "root must be a mapping");
    }

    CloseUpData data;
    data.version = root["version"] ? root["version"].as<int>() : 1;
    data.background_color = default_background_color;
    if (root["id"]) {
        data.id = root["id"].as<std::string>();
    } else if (!expected_id.empty()) {
        data.id = expected_id;
    } else {
        closeup_fail("closeup.id-missing", "'id' is required", root);
    }
    if (!expected_id.empty() && data.id != expected_id) {
        closeup_fail("closeup.id-mismatch",
                     "'id: " + data.id + "' does not match the close-up filename '" + expected_id +
                         "'",
                     root["id"]);
    }

    if (!root["background"]) {
        closeup_fail("closeup.background-missing",
                     "close-up '" + data.id + "': a 'background' image is required",
                     root);
    }
    data.background =
        resolve_background(root["background"].as<std::string>(), logical_path, root["background"]);

    if (const YAML::Node color = root["background_color"]) {
        data.background_color = sf::Color(color["r"].as<unsigned>(),
                                          color["g"].as<unsigned>(),
                                          color["b"].as<unsigned>(),
                                          color["a"] ? color["a"].as<unsigned>() : 255);
    }

    if (const YAML::Node hotspots = root["hotspots"]) {
        for (const auto& kv : hotspots) {
            CloseUpHotspot hs;
            hs.id = kv.first.as<std::string>();
            const YAML::Node node = kv.second;
            hs.name = node["name"] ? node["name"].as<std::string>() : hs.id;
            if (!node["area"]) {
                closeup_fail("closeup.hotspot-no-area",
                             "close-up '" + data.id + "': hotspot '" + hs.id + "' needs an 'area'",
                             node);
            }
            hs.area = parse_polygon(node["area"]);
            if (hs.area.size() < 3) {
                closeup_fail("closeup.hotspot-area-degenerate",
                             "close-up '" + data.id + "': hotspot '" + hs.id +
                                 "' area needs at least 3 points",
                             node["area"]);
            }
            if (node["goto"]) {
                hs.goto_scene = node["goto"].as<std::string>();
            }
            data.hotspots.push_back(std::move(hs));
        }
    }

    return data;
}

} // namespace pac::pnc
