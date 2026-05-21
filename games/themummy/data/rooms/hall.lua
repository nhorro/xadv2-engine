local room = {}

function room.on_load() end
function room.on_unload() end

room.on_zone_enter = function(zone)
  if zone == "to_study" then
    change_room("study", "from_hall")
  elseif zone == "to_exterior" then
    if get_room_state("salida.open") then
      change_room("exterior", "from_hall")
    else
      talk("julia", "La puerta de SALIDA esta cerrada con llave.")
    end
  end
end

room.hotspots = {
  cart = {
    look_at = function()
      return "Un carro con un sarcofago. Algo brilla detras."
    end,
    push = function()
      if get_region_state("cart") == "gone" then
        return "Ya lo empuje."
      end
      set_region_state("cart", "gone")
      set_room_state("cart.moved", true)
      add_item("key")
      return "Empujo el carro. Detras habia una llave: la agarro."
    end,
  },
  salida = {
    look_at = function()
      if get_room_state("salida.open") then
        return "La salida, ahora abierta."
      end
      return "La puerta de SALIDA. Cerrada con llave."
    end,
    open = function()
      return "Necesito una llave."
    end,
  },
}

return room
