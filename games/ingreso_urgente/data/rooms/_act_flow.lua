-- Per-act room flow helper (shared; not a room — loaded only via include()).
--
-- A room's behaviour usually differs per ACT. This helper keeps the entry script
-- tiny: split each act into its own module and let `flow.enter` dispatch.
--
-- Usage (from a room's <room>.lua):
--   local flow = include("rooms/_act_flow.lua")
--   local acts = {                              -- 1-based, indexed by <room>.cfg
--     include("rooms/<room>/_act1.lua"),
--     include("rooms/<room>/_act2.lua"),
--     ...
--   }
--   function room.on_load() flow.enter("<room>", acts) end
--
-- Each act module returns a table that MAY define:
--   configure(c)      INSTANT presence rebuild — spawn_npc/despawn_npc,
--                     show_object/hide_object, set_obstacle_enabled,
--                     set_region_state, etc. Runs on EVERY entry; must be
--                     exhaustive + idempotent (no waits/talk/move_to here).
--   on_first_enter()  BLOCKING beat the FIRST time the player enters this room in
--                     this act (one-time: intro lines, set_state flags, ...).
--                     Run inside spawn() automatically.
--   on_reenter()      BLOCKING beat on later entries in the same act (e.g. an NPC
--                     greeting: talk("npc", "¿Volviste?")). Run inside spawn().
--
-- "First time" is tracked per room+act in global state ("<room>.act<c>.seen"), so
-- it survives save/load and room unload.

local flow = {}

local function current_act(room_id, acts)
    local c = get_state(room_id .. ".cfg") or 1
    return c, acts[c]
end

-- Rebuild the current act's presence (INSTANT only). Call this from a transition
-- beat right after advancing "<room>.cfg" so the room reflects the new act.
function flow.configure(room_id, acts)
    local _, a = current_act(room_id, acts)
    if a and a.configure then
        local c = get_state(room_id .. ".cfg") or 1
        a.configure(c)
    end
end

-- Call from on_load: rebuild presence, then play the first-enter or re-enter beat.
function flow.enter(room_id, acts)
    local c, a = current_act(room_id, acts)
    if not a then
        return
    end
    if a.configure then
        a.configure(c) -- instant, every entry
    end
    local seen = room_id .. ".act" .. c .. ".seen"
    if not get_state(seen) then
        set_state(seen, true)
        if a.on_first_enter then
            spawn(a.on_first_enter) -- one-time beat for this act
        end
    elseif a.on_reenter then
        spawn(a.on_reenter) -- returning beat (e.g. "¿Volviste?")
    end
end

return flow
