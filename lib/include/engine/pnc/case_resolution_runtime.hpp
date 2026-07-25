#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace pac::core {
class Diagnostics;
class Scripting;
} // namespace pac::core

namespace pac::pnc {

/// Optional Lua behavior for a case-resolution scene. The sidecar returns a table:
///
///     return {
///       on_check = function(invalid_slots) ... end,
///       on_exit = function(status) ... end,
///     }
///
/// `on_check` runs only when every slot is filled. Zero invalid slots means the
/// case is resolved. The hook is direct/non-blocking; use spawn(...) if needed.
class CaseResolutionRuntime {
public:
    CaseResolutionRuntime();
    ~CaseResolutionRuntime();
    CaseResolutionRuntime(CaseResolutionRuntime&&) noexcept;
    CaseResolutionRuntime& operator=(CaseResolutionRuntime&&) noexcept;

    bool load(pac::core::Scripting& scripting,
              const std::string& lua_text,
              const std::string& logical,
              pac::core::Diagnostics& log);
    [[nodiscard]] bool valid() const;
    void run_on_check(std::size_t invalid_slots);
    void run_on_exit(const std::string& status);

private:
    struct Behavior;
    std::unique_ptr<Behavior> behavior_;
};

} // namespace pac::pnc
