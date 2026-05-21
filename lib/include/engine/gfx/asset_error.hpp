#pragma once

#include <stdexcept>

namespace pac::gfx {

class AssetError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace pac::gfx
