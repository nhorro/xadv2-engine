-- Global game logic for Ingreso Urgente. Returns a `game` table with:
--   * game.on_start()  -- one-time world-state init for a new game (see below)
--   * game.fallbacks   -- verb handlers run when a hotspot/item has none of its
--                          own (return a string to say something, or nothing for
--                          the engine's default caption from `strings.defaults`).
--
--==============================================================================
-- ROOM CONFIGURATIONS (#185)
--==============================================================================
-- Each room is shown in one of a few discrete *configurations* (which actors /
-- objects are present). These are now DECLARED in each room's YAML under
-- `configs:` and tracked by the engine — no "<room>.cfg" integer to seed here.
--
--   lab       intro / puzzle / alone
--   exterior  empty / delivery / box
--   hall      empty / box_closed / mummy
--
-- A beat switches config with `set_room_config("<room>", "<id>")` (it reconciles
-- presence and, for another room, takes effect when that room next loads); read
-- the live config with `current_room_config("<room>")`. The first-enter / re-enter
-- beats live in each room's `.lua` under `room.configs`. See design 06 §Room YAML.
--==============================================================================

local game = {}

-- Initial world state for a NEW game. Runs once when the game starts, BEFORE the
-- start room's on_load, and is SKIPPED on Continue/Load so it never overwrites a
-- save. Room configs default to each YAML's `start:` — seed only game-wide flags.
function game.on_start()
  -- Pre-puzzle progress flags (not config-derived): the lab's window distraction
  -- gate. schneider_present / puzzle_enabled are derived from the lab config now.
  set_state("act1.bones_glanced", false)
  set_state("act1.context_glanced", false)
  set_state("act1.thermo_checked", false)
  set_state("act1.window_distraction_enabled", false)

  -- DEV: uncomment to jump straight into the puzzle for testing (skips the intro).
  -- set_state("lab.julia_intro_seen", true)
  -- set_room_config("lab", "puzzle")
end

-- Shared verb fallbacks: run when a hotspot/item has no handler for a valid verb.
-- Return a string to say something, or nothing for the engine's default caption.
game.fallbacks = {
  -- look_at = function(target) return "No veo nada especial." end,
}

return game
