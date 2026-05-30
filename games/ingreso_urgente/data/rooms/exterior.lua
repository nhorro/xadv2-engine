-- Exterior room behaviour. Static layout lives in exterior.yaml.
--
-- ROOM CONFIGURATIONS (shared convention documented in scripts/game.lua)
-- Read from the global state key "exterior.cfg", rebuilt in on_load by configure():
--
--   CFG_EMPTY     Nobody; no delivery.
--   CFG_DELIVERY  The delivery guy + his truck + the box (urgent package arrives).
--   CFG_BOX       The box only — the institution accepted it, so the truck left.
--
-- The box/truck are objects and delivery_guy is a spawned NPC (see exterior.yaml).

local room = {}

local CFG_EMPTY = 1
local CFG_DELIVERY = 2
local CFG_BOX = 3

local function cfg()
    return get_state("exterior.cfg") or CFG_EMPTY
end

--------------------------------------------------------------------------------
-- Configuration setup (INSTANT calls only). Exhaustive + idempotent.
--------------------------------------------------------------------------------
local function configure(c)
    if c == CFG_DELIVERY then
        spawn_npc("delivery_guy", "delivery_guy_start", "down")
        show_object("truck")
        show_object("box")
    elseif c == CFG_BOX then
        despawn_npc("delivery_guy")
        hide_object("truck")
        show_object("box")
    else -- CFG_EMPTY
        despawn_npc("delivery_guy")
        hide_object("truck")
        hide_object("box")
    end
end

--------------------------------------------------------------------------------
-- Story beats (BLOCKING — run inside spawn()).
--------------------------------------------------------------------------------

-- The truck arrives with the urgent package: EMPTY -> DELIVERY.
function delivery_arrives()
    block_input()
    -- show_text("Una camioneta se detuvo frente al instituto.")
    -- (a static box/truck just appear; if `truck` is animated you could
    --  object("truck"):move_to("truck_spot") for a drive-in.)
    set_state("exterior.cfg", CFG_DELIVERY)
    configure(CFG_DELIVERY)
    unblock_input()
end

-- The institution accepts the box; the guy + truck leave: DELIVERY -> BOX.
function delivery_leaves()
    block_input()
    -- talk("delivery_guy", "Listo, les dejo el paquete. ¡Que tengan buen día!")
    -- object("truck"):move_to("offscreen_right")  -- needs a point; truck as object
    despawn_npc("delivery_guy")
    hide_object("truck")
    set_state("exterior.cfg", CFG_BOX)
    configure(CFG_BOX)
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
    door = {
        look_at = function()
            return "La puerta de ingreso al instituto."
        end,
        open = function()
            -- play_sound("sfx/door_open.ogg")
            change_room("hall", "from_exterior")
        end,
    },

    -- Handlers for the npc-bound hotspot in exterior.yaml (issue #141).
    -- Active only while delivery_guy is present (CFG_DELIVERY).
    -- delivery_guy = {
    --   look_at = function() return "El repartidor, esperando junto a la camioneta." end,
    --   talk_to = function() start_dialog("delivery_guy") end,
    -- },

    -- box/truck hotspots are bound to their objects in exterior.yaml, so they
    -- deactivate when hidden. Add handlers here when the interactions are defined:
    -- box = {
    --   look_at = function() return "La caja del envío urgente." end,
    --   open    = function() return "Mejor no la abro acá afuera." end,
    -- },
    -- truck = {
    --   look_at = function() return "La camioneta del repartidor." end,
    -- },
}

return room
