#pragma once

#include <stdexcept>

namespace pac::pnc {

/// Thrown by the genre data loaders (room, cast) on malformed input.
class DataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace pac::pnc
