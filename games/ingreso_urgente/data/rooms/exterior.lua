-- Exterior room behaviour. Static layout lives in exterior.yaml.
--
-- Configurations (#185) are declared in exterior.yaml under `configs:` (presence:
-- delivery_guy / truck / box / floor obstacles), keyed by a symbolic id. The
-- engine reconciles presence on entry; here we supply only the beats + transitions:
--
--   empty     Nobody; no delivery.
--   delivery  The delivery guy + his truck + the box (urgent package).
--   box       The box only — the institution accepted it, the truck left.
--
-- `set_room_config("exterior", <id>)` switches config; `current_room_config(...)`
-- reads it. The box/truck are objects, delivery_guy a spawned NPC (exterior.yaml).

local room = {}

-- Optional per-config beats, keyed like exterior.yaml's `configs:`. Presence-only
-- configs (empty, box) need no entry. Uncomment to give the delivery a voice:
-- room.configs = {
--   delivery = {
--     on_first_enter = cutscene(function()
--       talk("delivery_guy", "¡Buenas! Traigo un envío urgente para el instituto.")
--     end),
--     on_reenter = cutscene(function()
--       talk("delivery_guy", "¿Seguís dando vueltas? El paquete no se entrega solo.")
--     end),
--   },
-- }

--------------------------------------------------------------------------------
-- Transitions between configs (global so cutscenes / other rooms can drive them).
--------------------------------------------------------------------------------

-- The truck arrives with the urgent package: empty -> delivery.
function delivery_arrives()
    set_room_config("exterior", "delivery")
end

-- The institution accepts the box; the guy + truck leave: delivery -> box.
function delivery_leaves()
    set_room_config("exterior", "box")
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
