#pragma once

// Internal helper shared by the room and cast loaders: parses the `shader:` /
// `shaders:` block on a drawable owner (background layer, region, object,
// appearance). Diagnostics surface through whichever loader's DataError tag is
// passed in, so error codes stay stable per loader.

#include "engine/gfx/shader_effect.hpp"

#include <string>
#include <vector>

namespace YAML {
class Node;
}

namespace pac::pnc::detail {

/// Parse the optional `shader:` and `shaders:` keys on `owner`. Throws a
/// pac::pnc::DataError with `source` and a `<code_prefix>.shader-*` error code on
/// malformed input. Both keys may appear; `shader` (single) is placed first, then
/// `shaders` (ordered list) appends.
std::vector<gfx::ShaderEffect>
parse_shaders(const YAML::Node& owner, const char* source, const std::string& code_prefix);

} // namespace pac::pnc::detail
