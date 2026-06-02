#pragma once

#include "engine/geom/geometry.hpp"

#include <SFML/Graphics/Color.hpp>

#include <string>
#include <vector>

namespace pac::pnc {

/// One examinable region of a close-up: a hit-test polygon (in the close-up's
/// virtual-resolution space) with a localized `name` shown as a look caption, and
/// an optional `goto_scene` outcome that switches scenes when clicked (issue #76).
struct CloseUpHotspot {
    std::string id;
    std::string name;       // localized caption / hover label
    geom::Polygon area;     // hit-test polygon
    std::string goto_scene; // optional scene id to switch to on click
};

/// Parsed close-up / examine view (`closeups/<id>.yaml`): a full-screen background
/// plus hotspots, each with a single action. Headless and testable — no graphics.
struct CloseUpData {
    int version = 1;
    std::string id;
    std::string background; // full-screen background image (resolved logical path)
    sf::Color background_color = sf::Color::Black;
    std::vector<CloseUpHotspot> hotspots;

    /// The first hotspot whose polygon contains `p`, or nullptr.
    [[nodiscard]] const CloseUpHotspot* hotspot_at(geom::Point p) const;
};

/// Parse + validate close-up YAML. Throws a `pac::core::LoadError` (source
/// `closeup-loader`) on malformed input. When `expected_id` is non-empty it must
/// match the YAML `id` (the filename-vs-id check); pass it empty to skip (e.g.
/// headless tests feeding raw text).
///
/// `background` resolves relative to `logical_path`'s directory (the close-up YAML
/// file), so a co-located image is just `background: background.png`. A leading
/// `/` makes it resources-root-relative instead (`/rooms/b/bg.png`), for sharing
/// an asset from another directory. When `logical_path` is empty the value is used
/// verbatim (back-compat for headless tests).
CloseUpData parse_closeup(const std::string& yaml_text,
                          const std::string& expected_id = {},
                          const std::string& logical_path = {});

} // namespace pac::pnc
