-- Global game logic for Ingreso Urgente. Returns a `game` table with:
--   * game.on_start()  -- one-time world-state init for a new game (see below)
--   * game.fallbacks   -- verb handlers run when a hotspot/item has none of its
--                          own (return a string to say something, or nothing for
--                          the engine's default caption from `strings.defaults`).
--
--==============================================================================
-- ROOM CONFIGURATION CONVENTION (skeleton — arrange the beats per the script)
--==============================================================================
-- Each room is shown in one of a few discrete *configurations*: which actors
-- and objects are present, plus scenery state. The current configuration of a
-- room is a global saved-state integer keyed "<room>.cfg":
--
--     get_state("lab.cfg")  set_state("lab.cfg", N)   -- lab
--     get_state("hall.cfg")                            -- hall
--     get_state("exterior.cfg")                        -- exterior
--
-- Why GLOBAL state (not set_room_state): a beat in one room often advances
-- another room's config (accepting the box in the exterior makes it appear in
-- the hall), and global state survives save/load and room unload. Room state
-- can only be read/written for the *current* room.
--
-- Each room file declares named CFG_* constants and two layers of code:
--
--   1. configure(c)  -- INSTANT calls only (spawn_npc/despawn_npc, show_object/
--                       hide_object, set_layer_visible, set_region_state).
--                       Exhaustive + idempotent: it spawns what belongs in `c`
--                       and despawns/hides the rest, so on_load can rebuild any
--                       config from scratch. NPC presence is NOT persisted (it
--                       is re-derived every load); object/layer visibility IS.
--
--   2. story beats   -- BLOCKING sequences (wait/talk/move_to/show_text). They
--                       must run inside spawn(). Each beat plays its cutscene/
--                       dialog, then set_state("<room>.cfg", NEXT) + configure().
--
--   on_load() simply calls configure(cfg()); a beat (or the intro) is triggered
--   explicitly where the script calls for it.
--
-- Planned configurations (see design_files/STORY_AND_PUZZLES.md for the script):
--
--   lab.cfg       1 INTRO      Julia alone, opening monologue
--                 2 PUZZLE     Julia + Dr. Schneider, skull-trauma puzzle
--                 3 ALONE      Schneider has left, Julia alone again
--
--   exterior.cfg  1 EMPTY      nobody
--                 2 DELIVERY   delivery guy + truck + box
--                 3 BOX        box only (truck left once the box was accepted)
--
--   hall.cfg      1 EMPTY      nobody
--                 2 BOX_CLOSED the closed box
--                 3 MUMMY      the mummy (was inside the box)
--                              + guests spawned/despawned between cutscenes
--
-- A "text cutscene" inside a room is just show_text(...) (a speaker-less page)
-- inside a beat — used e.g. to make Dr. Schneider appear/disappear off-screen.
--
-- TODO (data): exterior.yaml — move `box`/`truck` from layers to objects and
-- make `delivery_guy` a spawned NPC (not a declared avatar); hall.yaml — add a
-- `box` object and the `mummy` (object or NPC). See each room file's header.
--==============================================================================

local game = {}

-- Initial world state for a NEW game. Runs once when the game starts, BEFORE the
-- start room's on_load (so each room's on_load reads the config set here), and is
-- SKIPPED on Continue/Load, so it never overwrites a save. Values per the table
-- above; keep them in sync with each room file's CFG_* constants.
function game.on_start()
  set_state("lab.cfg", 1)      -- CFG_INTRO   Julia alone, opening monologue
  set_state("exterior.cfg", 1) -- CFG_EMPTY   nobody outside yet
  set_state("hall.cfg", 1)     -- CFG_EMPTY   nobody in the hall yet

  

  -- transición previa al puzzle formal 
  set_state("act1.bones_glanced", false)
  set_state("act1.context_glanced", false)
  set_state("act1.thermo_checked", false)
  set_state("act1.window_distraction_enabled", false)
  set_state("act1.schneider_present", false)
  set_state("act1.puzzle_enabled", false)

  -- DEV
  -- Dev: uncomment to jump to a later beat for testing.
  -- set_state("lab.julia_intro_seen", true)
  -- set_state("lab.cfg", 2)      -- CFG_PUZZLE   Julia + Dr. Schneider, skull-trauma puzzle
end

-- Shared verb fallbacks: run when a hotspot/item has no handler for a valid verb.
-- Return a string to say something, or nothing for the engine's default caption.
game.fallbacks = {
  -- look_at = function(target) return "No veo nada especial." end,
}

return game
