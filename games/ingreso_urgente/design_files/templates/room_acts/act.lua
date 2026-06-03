-- TEMPLATE: data/rooms/<room>/_act<N>.lua  (one per act / "<room>.cfg" value).
-- Returns a table the flow helper dispatches to. All three hooks are optional.
local M = {}

-- INSTANT presence rebuild for this act. Runs on EVERY room entry while in this
-- act; must be exhaustive + idempotent. NO waits/talk/move_to here.
function M.configure(c)
    -- spawn_npc("npc_id", "start_point", "down")   -- or despawn_npc("npc_id")
    -- show_object("thing")                          -- or hide_object("thing")
    -- set_obstacle_enabled("crates", true)
    -- set_region_state("puddle", "wet")
end

-- BLOCKING beat the FIRST time the player enters the room in this act (one-time:
-- intro lines, set_state flags...). Auto-spawn()ed by the flow helper.
-- function M.on_first_enter()
--     talk("player", "...")
--     set_state("<room>.act<N>.intro_done", true)
-- end

-- BLOCKING beat on later entries in the same act (e.g. an NPC greeting).
-- function M.on_reenter()
--     talk("npc", "¿Volviste?")
-- end

-- Optional act-specific hotspot handlers, if the entry script dispatches to them.
-- M.hotspots = {
--     thing = { use = function() ... end, look_at = function() return "..." end },
-- }

return M
