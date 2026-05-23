#pragma once

#include "engine/core/load_error.hpp"

#include <string>

namespace pac::gfx {

/// Thrown by the asset loaders (animation, spritesheet) on malformed input. A
/// `pac::core::LoadError` carrying the `{ source, location, id, message }`
/// envelope; existing `catch (std::exception&)` sites keep working.
class AssetError : public pac::core::LoadError {
public:
    using pac::core::LoadError::LoadError;
    explicit AssetError(const std::string& message)
        : pac::core::LoadError("asset-loader", "", message) {}
};

} // namespace pac::gfx
