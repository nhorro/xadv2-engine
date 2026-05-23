local room = {}

function room.on_load() end
function room.on_unload() end

room.on_zone_enter = function(zone)
  if zone == "to_study" then
    change_room("study", "from_hall")
  end
end

room.hotspots = {
  mummy = {
    look_at = function()
      return "Una momia en un carro con un sarcofago. Algo brilla detras."
    end,
    push = function()      
      set_room_state("mummy.moved", true)
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
      if get_room_state("salida.open") then
        change_room("exterior", "from_hall")
      else        
        -- Returning nothing and just talking doesn't work, we need to review this.
        -- For now, let's return the text here.         
        --talk("julia", "La puerta de SALIDA esta cerrada con llave.")
        return("Intento abrir la puerta, pero esta cerrada con llave.")
      end      
    end,
  },
}

return room
