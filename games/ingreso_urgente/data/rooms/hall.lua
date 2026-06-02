-- Hall room behaviour. Static layout lives in hall.yaml.
--
-- Per-act split (shared convention in scripts/game.lua, helper rooms/_act_flow.lua):
-- per-act presence lives in rooms/hall/_act<N>.lua, dispatched by the flow helper.
-- Read from the global key "hall.cfg":
--
--   CFG_EMPTY       1  Nobody, nothing.
--   CFG_BOX_CLOSED  2  The closed box sits in the hall (carried in from outside).
--   CFG_MUMMY       3  The mummy — it was inside the box.
--
-- Extra actors (e.g. Dr. Schneider inspecting) combine freely with the box/mummy:
-- spawn/despawn them inside the story beats, between cutscenes.

local flow = include("rooms/_act_flow.lua")

local CFG_EMPTY = 1
local CFG_BOX_CLOSED = 2
local CFG_MUMMY = 3

-- 1-based, indexed by "hall.cfg".
local acts = {
    include("rooms/hall/_act1_empty.lua"),
    include("rooms/hall/_act2_box_closed.lua"),
    include("rooms/hall/_act3_mummy.lua"),
}

local room = {}

function room.on_load()
    flow.enter("hall", acts)
end

function room.on_unload() end

--------------------------------------------------------------------------------
-- Transitions between acts (BLOCKING, global so cutscenes/other rooms can drive
-- them). Advance "hall.cfg", then rebuild presence with flow.configure (INSTANT).
--------------------------------------------------------------------------------

-- The box is carried in from the exterior: EMPTY -> BOX_CLOSED.
function box_arrives()
    block_input()
    -- show_text("Trasladaron la caja al pasillo del instituto.")
    set_state("hall.cfg", CFG_BOX_CLOSED)
    flow.configure("hall", acts)
    unblock_input()
end

-- The box is opened and the mummy emerges: BOX_CLOSED -> MUMMY.
function mummy_revealed()
    block_input()
    -- show_text("La caja se abrió sola. Adentro había una momia.")
    set_state("hall.cfg", CFG_MUMMY)
    flow.configure("hall", acts)
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
room.hotspots = {
    lab_door = {
        look_at = function()
            return "La puerta da al laboratorio del instituto."
        end,
        use = function()
            -- play_sound("sfx/door_open.ogg")
            change_room("lab", "from_hall")
        end,
    },
    exterior_door = {
        look_at = function()
            return "La puerta da al exterior del instituto."
        end,
        use = function()
            -- play_sound("sfx/door_open.ogg")
            change_room("exterior", "from_hall")
        end,
    },

    -- Once a `box` / `mummy` object (or npc) exists, add handlers here:
    -- box = {
    --   look_at = function() return "La caja del envío urgente, todavía cerrada." end,
    --   use     = function() mummy_revealed(); return "" end,
    -- },
    -- mummy = {
    --   look_at = function() return "Una momia. Definitivamente no estaba en el inventario." end,
    -- },
}

return room
