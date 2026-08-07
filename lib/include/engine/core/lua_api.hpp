#pragma once

#include <functional>
#include <string>

namespace pac::core {

struct EngineContext;
class FactsRegistry;
class Scripting;

/// Register the core (layer-agnostic) Lua API as flat snake_case globals: flow is
/// provided by the scripting service (`spawn`/`wait`/`emit`/`wait_event`); this
/// adds `resource_path`, audio (`play_music`/`crossfade_music`/`stop_music`/`play_sound`/
/// `stop_sound`/`stop_sounds`), and state (`get_state`/`set_state`). Genre APIs (talk,
/// change_room, ...) are registered by the point-and-click layer in later milestones.
/// Also wires the declared-facts proxy (issue #188): if a `facts.yaml` resource is
/// present it is parsed and bound via `bind_facts` below.
void bind_core_api(EngineContext& ctx);

/// Bind the `facts.<ns>.<name>` proxy (issue #188): nested read and assignment
/// route through the already-bound `get_state`/`set_state`, so a fact persists and
/// interoperates with the dotted state key. When `dev_warn` is true, reading or
/// writing a key outside `reg` invokes `warn` (the typo guard); an empty `reg`
/// (no `facts.yaml`) leaves the proxy working with the guard disabled. Exposed
/// separately so tests can drive it with a recording `warn`; `bind_core_api` wires
/// `dev_warn` to the build (loud in Debug, opt-in via `development.show_state` in
/// Release) and routes `warn` to the engine Diagnostics. Requires
/// `get_state`/`set_state` to be bound already.
void bind_facts(Scripting& scripting,
                const FactsRegistry& reg,
                bool dev_warn,
                std::function<void(const std::string&)> warn);

} // namespace pac::core
