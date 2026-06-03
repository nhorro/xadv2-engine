-- Exterior room behaviour. Static layout lives in exterior.yaml.
--
-- This room is split per CONFIGURATION (the convention in scripts/game.lua): each
-- config's presence + enter beats live in rooms/exterior/<act>/<role>.lua,
-- dispatched by the shared rooms/_room_flow.lua helper. Read from "exterior.cfg":
--
--   CFG_EMPTY     1  Nobody; no delivery.
--   CFG_DELIVERY  2  The delivery guy + his truck + the box (urgent package).
--   CFG_BOX       3  The box only — the institution accepted it, the truck left.
--
-- The box/truck are objects and delivery_guy is a spawned NPC (see exterior.yaml).

local flow = include("rooms/_room_flow.lua")

local CFG_EMPTY = 1
local CFG_DELIVERY = 2
local CFG_BOX = 3

-- 1-based, indexed by "exterior.cfg".
local configs = {
    include("rooms/exterior/act1/empty.lua"),
    include("rooms/exterior/act1/delivery.lua"),
    include("rooms/exterior/act1/box.lua"),
}

local room = {}

function room.on_load()
    flow.enter("exterior", configs)
end

function room.on_unload() end

--------------------------------------------------------------------------------
-- Transitions between configs (BLOCKING — these are global so cutscenes/other rooms
-- can drive them). Each advances "exterior.cfg", then rebuilds presence via
-- flow.configure (INSTANT).
--------------------------------------------------------------------------------

-- The truck arrives with the urgent package: EMPTY -> DELIVERY.
function delivery_arrives()
    block_input()
    -- show_text("Una camioneta se detuvo frente al instituto.")
    set_state("exterior.cfg", CFG_DELIVERY)
    flow.configure("exterior", configs)
    unblock_input()
end

-- The institution accepts the box; the guy + truck leave: DELIVERY -> BOX.
function delivery_leaves()
    block_input()
    -- talk("delivery_guy", "Listo, les dejo el paquete. ¡Que tengan buen día!")
    set_state("exterior.cfg", CFG_BOX)
    flow.configure("exterior", configs)
    unblock_input()
end

--------------------------------------------------------------------------------
-- Hotspots. The door is constant across configs; per-config hotspots (delivery_guy,
-- box, truck) can either dispatch to the active act module or branch on cfg here.
--------------------------------------------------------------------------------
room.hotspots = {
    door = {
        look_at = function()
            return "La puerta de ingreso al instituto."
        end,
        use = function()
            -- play_sound("sfx/door_open.ogg")
            change_room("hall", "from_exterior")
        end,
    },
}

return room
