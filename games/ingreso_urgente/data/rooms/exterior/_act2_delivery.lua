-- exterior — cfg 2: DELIVERY. The delivery guy + his truck + the box.
local M = {}

function M.configure(c)
    spawn_npc("delivery_guy", "delivery_guy_start", "down")
    show_object("truck")
    show_object("box")
end

-- First time the player sees the delivery: the guy hails them.
function M.on_first_enter()
    -- talk("delivery_guy", "¡Buenas! Traigo un envío urgente para el instituto.")
end

-- Coming back while the delivery is still pending: a nudge.
function M.on_reenter()
    -- talk("delivery_guy", "¿Seguís dando vueltas? El paquete no se entrega solo.")
end

return M
