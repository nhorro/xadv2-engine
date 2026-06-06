-- Hall room behaviour. Static layout lives in hall.yaml.
--
-- Configurations (#185) are declared in hall.yaml under `configs:` (presence: the
-- box), keyed by a symbolic id. The engine reconciles presence on entry:
--
--   empty       Nobody, nothing.
--   box_closed  The closed box sits in the hall (carried in from outside).
--   mummy       The mummy — it was inside the box (its art/cast still TODO).
--
-- `set_room_config("hall", <id>)` switches config. Extra actors (e.g. Dr.
-- Schneider inspecting) combine freely with the standing config via spawn/despawn
-- inside the beats below.

local room = {}

-- (No per-config beats yet — presence-only. Add `room.configs = { ... }` here when
-- a config wants a first-enter / re-enter line.)

--------------------------------------------------------------------------------
-- Transitions between configs (global so cutscenes / other rooms can drive them).
--------------------------------------------------------------------------------

-- The box is carried in from the exterior: empty -> box_closed.
function box_arrives()
    set_room_config("hall", "box_closed")
end

-- The box is opened and the mummy emerges: box_closed -> mummy.
function mummy_revealed()
    set_room_config("hall", "mummy")
end

-- Narration + a guest actor: time passes, then Dr. Schneider inspects the mummy.
-- Shows how a temporary NPC combines with the standing config (spawn/despawn is
-- still available alongside the declarative presence).
function schneider_inspects_mummy()
    -- block_input()
    -- show_text("La momia permaneció durante horas, hasta que la Dra. Schneider se acercó al pasillo para inspeccionarla.")
    -- spawn_npc("schneider", "from_lab", "left")
    -- avatar("schneider"):move_to("at_mummy")
    -- avatar("schneider"):face("up")
    -- talk("schneider", "Imposible. Esto no estaba catalogado...")
    -- despawn_npc("schneider")
    -- unblock_input()
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
