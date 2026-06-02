-- Exterior room behaviour. Static layout lives in exterior.yaml.
--
-- This room is split per ACT (the shared convention in scripts/game.lua): the
-- per-act presence + enter beats live in rooms/exterior/_act<N>.lua, dispatched by
-- the shared rooms/_act_flow.lua helper. Read from the global key "exterior.cfg":
--
--   CFG_EMPTY     1  Nobody; no delivery.
--   CFG_DELIVERY  2  The delivery guy + his truck + the box (urgent package).
--   CFG_BOX       3  The box only — the institution accepted it, the truck left.
--
-- The box/truck are objects and delivery_guy is a spawned NPC (see exterior.yaml).

local flow = include("rooms/_act_flow.lua")

local CFG_EMPTY = 1
local CFG_DELIVERY = 2
local CFG_BOX = 3

-- 1-based, indexed by "exterior.cfg".
local acts = {
    include("rooms/exterior/_act1_empty.lua"),
    include("rooms/exterior/_act2_delivery.lua"),
    include("rooms/exterior/_act3_box.lua"),
}

local room = {}

function room.on_load()
    flow.enter("exterior", acts)
end

function room.on_unload() end

--------------------------------------------------------------------------------
-- Transitions between acts (BLOCKING — these are global so cutscenes/other rooms
-- can drive them). Each advances "exterior.cfg", then rebuilds presence via
-- flow.configure (INSTANT).
--------------------------------------------------------------------------------

-- The truck arrives with the urgent package: EMPTY -> DELIVERY.
function delivery_arrives()
    block_input()
    -- show_text("Una camioneta se detuvo frente al instituto.")
    set_state("exterior.cfg", CFG_DELIVERY)
    flow.configure("exterior", acts)
    unblock_input()
end

-- The institution accepts the box; the guy + truck leave: DELIVERY -> BOX.
function delivery_leaves()
    block_input()
    -- talk("delivery_guy", "Listo, les dejo el paquete. ¡Que tengan buen día!")
    set_state("exterior.cfg", CFG_BOX)
    flow.configure("exterior", acts)
    unblock_input()
end

--------------------------------------------------------------------------------
-- Hotspots. The door is constant across acts; act-specific hotspots (delivery_guy,
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
