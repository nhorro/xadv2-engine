-- lab — cfg 2: PUZZLE. Julia + Dr. Schneider; the skull-trauma puzzle is active.
local M = {}

-- INSTANT presence: Schneider at rest (this restores an already-set PUZZLE state,
-- e.g. on reload). The walk-in choreography is the schneider_arrives() transition
-- in lab.lua, which advances the act the first time.
function M.configure(c)
    spawn_npc("schneider", "schneider_start", "left")
    set_state("act1.schneider_present", true)
    set_state("act1.puzzle_enabled", true)
end

-- Returning to the lab while the puzzle is on (after the arrival): Schneider could
-- prod the player. Left as an example; uncomment to enable.
-- function M.on_reenter()
--     talk("schneider", "¿Alguna conclusión, Serrategui, o seguimos contemplando?")
-- end

return M
