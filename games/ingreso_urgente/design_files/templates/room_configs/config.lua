-- TEMPLATE: data/rooms/<room>/act<N>/<role>.lua  (one per "<room>.cfg" value).
-- Returns a table the flow helper dispatches to. All hooks are optional.
local M = {}

-- INSTANT presence rebuild for this config. Runs on EVERY room entry while in
-- this config; must be exhaustive + idempotent. NO waits/talk/move_to here.
function M.configure(c)
    -- spawn_npc("npc_id", "start_point", "down")   -- or despawn_npc("npc_id")
    -- show_object("thing")                          -- or hide_object("thing")
    -- enable_obstacle("crates")                     -- or disable_obstacle("crates")
    -- set_region_state("puddle", "wet")
end

-- BLOCKING beat the FIRST time the player enters the room in this config
-- (one-time: intro lines, set_state flags...). Auto-spawn()ed by the flow helper.
-- function M.on_first_enter()
--     talk("player", "...")
-- end

-- BLOCKING beat on later entries in the same config (e.g. an NPC greeting).
-- function M.on_reenter()
--     talk("npc", "¿Volviste?")
-- end

return M
