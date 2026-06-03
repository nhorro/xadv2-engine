-- Per-configuration room flow helper (shared; not a room — loaded via include()).
--
-- A room is shown in one of several CONFIGURATIONS, indexed by the global integer
-- "<room>.cfg" (the convention in scripts/game.lua). A story act adds more
-- configs to that flat index. This helper keeps the entry script tiny: split each
-- config into its own module and let `flow.enter` dispatch.
--
-- Usage (from a room's <room>.lua):
--   local flow = include("rooms/_room_flow.lua")
--   local configs = {                            -- 1-based, indexed by <room>.cfg
--     include("rooms/<room>/act1/<role>.lua"),   -- grouped by act for readability
--     ...
--   }
--   function room.on_load() flow.enter("<room>", configs) end
--
-- Each config module returns a table that MAY define:
--   configure(c)      INSTANT presence rebuild — spawn_npc/despawn_npc,
--                     show_object/hide_object, enable_obstacle/disable_obstacle,
--                     set_region_state, etc. Runs on EVERY entry; must be
--                     exhaustive + idempotent (no waits/talk/move_to here).
--   on_first_enter()  BLOCKING beat the FIRST time the player enters this room in
--                     this config (one-time: intro lines, set_state flags, ...).
--                     Run inside spawn() automatically.
--   on_reenter()      BLOCKING beat on later entries in the same config (e.g. an
--                     NPC greeting: talk("npc", "¿Volviste?")). Run inside spawn().
--
-- "First time" is tracked per room+config in global state ("<room>.cfg<c>.seen"),
-- so it survives save/load and room unload.

local flow = {}

local function current_config(room_id, configs)
    local c = get_state(room_id .. ".cfg") or 1
    return c, configs[c]
end

-- Rebuild the current config's presence (INSTANT only). Call this from a
-- transition beat right after advancing "<room>.cfg" so the room reflects it.
function flow.configure(room_id, configs)
    local c, cfg = current_config(room_id, configs)
    if cfg and cfg.configure then
        cfg.configure(c)
    end
end

-- Call from on_load: rebuild presence, then play the first-enter or re-enter beat.
function flow.enter(room_id, configs)
    local c, cfg = current_config(room_id, configs)
    if not cfg then
        return
    end
    if cfg.configure then
        cfg.configure(c) -- instant, every entry
    end
    local seen = room_id .. ".cfg" .. c .. ".seen"
    if not get_state(seen) then
        set_state(seen, true)
        if cfg.on_first_enter then
            spawn(cfg.on_first_enter) -- one-time beat for this config
        end
    elseif cfg.on_reenter then
        spawn(cfg.on_reenter) -- returning beat (e.g. "¿Volviste?")
    end
end

return flow
