-- Hall room behaviour. Static layout lives in hall.yaml.
--
-- ROOM CONFIGURATIONS (shared convention documented in scripts/game.lua)
-- Read from the global state key "hall.cfg", rebuilt in on_load by configure():
--
--   CFG_EMPTY       Nobody, nothing.
--   CFG_BOX_CLOSED  The closed box sits in the hall (moved in from outside).
--   CFG_MUMMY       The mummy — it was inside the box.
--
-- Extra actors (e.g. Dr. Schneider coming to inspect) combine freely with the
-- box/mummy: spawn/despawn them inside the story beats, between cutscenes.
--
-- REQUIRED YAML CHANGES (hall.yaml) before the show/hide + spawn calls work:
--   * Add a `box` object (and an "open box" variant or a `mummy` — object or
--     spawned NPC) under `objects:`.
--   * If `mummy` is a spawned NPC, add its cast entry + an npc-bound hotspot;
--     add the room points the beats move actors to (e.g. at_mummy).

local room = {}

local CFG_EMPTY = 1
local CFG_BOX_CLOSED = 2
local CFG_MUMMY = 3

local function cfg()
    return get_state("hall.cfg") or CFG_EMPTY
end

--------------------------------------------------------------------------------
-- Configuration setup (INSTANT calls only). Exhaustive + idempotent.
--------------------------------------------------------------------------------
local function configure(c)
    if c == CFG_BOX_CLOSED then
        show_object("box")
        -- despawn_npc("mummy")
    elseif c == CFG_MUMMY then
        hide_object("box") -- or swap to an "open box" object / region state
        -- spawn_npc("mummy", "at_mummy", "down")
    else -- CFG_EMPTY
        hide_object("box")
        -- despawn_npc("mummy")
    end
end

--------------------------------------------------------------------------------
-- Story beats (BLOCKING — run inside spawn()).
--------------------------------------------------------------------------------

-- The box is carried in from the exterior: EMPTY -> BOX_CLOSED.
function box_arrives()
    block_input()
    -- show_text("Trasladaron la caja al pasillo del instituto.")
    set_state("hall.cfg", CFG_BOX_CLOSED)
    configure(CFG_BOX_CLOSED)
    unblock_input()
end

-- The box is opened and the mummy emerges: BOX_CLOSED -> MUMMY.
function mummy_revealed()
    block_input()
    -- show_text("La caja se abrió sola. Adentro había una momia.")
    set_state("hall.cfg", CFG_MUMMY)
    configure(CFG_MUMMY)
    unblock_input()
end

-- Narration + a guest actor: time passes, then Dr. Schneider inspects the mummy.
-- Shows how a temporary NPC combines with the standing config.
function schneider_inspects_mummy()
    block_input()
    -- show_text("La momia permaneció durante horas, hasta que la Dra. Schneider se acercó al pasillo para inspeccionarla.")
    -- spawn_npc("schneider", "from_lab", "left")
    -- avatar("schneider"):move_to("at_mummy")
    -- avatar("schneider"):face("up")
    -- talk("schneider", "Imposible. Esto no estaba catalogado...")
    -- despawn_npc("schneider")
    unblock_input()
end

--------------------------------------------------------------------------------
function room.on_load()
    configure(cfg())
end

function room.on_unload() end

-- room.on_zone_enter = function(zone)
--   if zone == "to_hall" then
--     change_room("hall", "from_study")
--   end
-- end

room.hotspots = {
    lab_door = {
        look_at = function()
            return "La puerta da al laboratorio del instituto."
        end,
        open = function()
            -- play_sound("sfx/door_open.ogg")
            change_room("lab", "from_hall")
        end,
    },
    exterior_door = {
        look_at = function()
            return "La puerta da al exterior del instituto."
        end,
        open = function()
            -- play_sound("sfx/door_open.ogg")
            change_room("exterior", "from_hall")
        end,
    },

    -- Once a `box` / `mummy` object (or npc) exists, add handlers here:
    -- box = {
    --   look_at = function() return "La caja del envío urgente, todavía cerrada." end,
    --   open    = function() mummy_revealed(); return "" end,
    -- },
    -- mummy = {
    --   look_at = function() return "Una momia. Definitivamente no estaba en el inventario." end,
    -- },
}

return room
