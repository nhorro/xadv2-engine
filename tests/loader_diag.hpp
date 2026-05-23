#pragma once

// Shared test helper: run a loader call and report the stable error `code` it
// throws via the `pac::core::LoadError` envelope, so tests can assert on the
// machine-matchable code instead of fragile message text.

#include "engine/core/load_error.hpp"

#include <string>

namespace pac::test {

template <class F>
std::string error_code(F f) {
    try {
        f();
    } catch (const pac::core::LoadError& e) {
        return e.code();
    }
    return "<no-load-error>";
}

} // namespace pac::test
