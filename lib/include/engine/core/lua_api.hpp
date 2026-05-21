#pragma once

namespace pac::core {

struct EngineContext;

/// Register the core (layer-agnostic) Lua API as flat snake_case globals: flow is
/// provided by the scripting service (`spawn`/`wait`/`emit`/`wait_event`); this
/// adds `resource_path`, audio (`play_music`/`stop_music`/`play_sound`/
/// `stop_sounds`), and state (`get_state`/`set_state`). Genre APIs (talk,
/// change_room, ...) are registered by the point-and-click layer in later milestones.
void bind_core_api(EngineContext& ctx);

} // namespace pac::core
