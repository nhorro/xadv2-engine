#pragma once

#include "engine/core/scripting.hpp"

#include <optional>
#include <string>

namespace pac::pnc {

/// Outcome of a verb-handler lookup. A matching handler owns the command even
/// when it returns no caption. In-flight handlers keep command input blocked
/// until their task drains.
struct VerbResult {
    bool handled = false;
    std::optional<std::string> caption;
    std::optional<pac::core::TaskId> in_flight;
};

} // namespace pac::pnc
