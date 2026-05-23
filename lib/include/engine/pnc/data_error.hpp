#pragma once

#include "engine/core/load_error.hpp"

#include <string>

namespace pac::pnc {

/// Thrown by the genre data loaders (room, cast, inventory) on malformed input.
/// A `pac::core::LoadError` so it carries the `{ source, location, id, message }`
/// envelope and is caught by existing `catch (std::exception&)` sites unchanged.
class DataError : public pac::core::LoadError {
public:
    using pac::core::LoadError::LoadError;
    explicit DataError(const std::string& message)
        : pac::core::LoadError("data-loader", "", message) {}
};

} // namespace pac::pnc
