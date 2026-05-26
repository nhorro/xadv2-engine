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
      -- Examine the sarcophagus up close (issue #76): opens an overlay close-up
      -- with its own hotspots; Esc / right-click returns to the hall.
      open_closeup("sarcophagus_closeup")
      return "Me acerco a examinar el sarcofago."
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
        play_sound("sfx/door_open.ogg")
        change_room("exterior", "from_hall")
      else
        -- A locked-door rattle gives the failed attempt some feedback.
        play_sound("sfx/door_locked.ogg")
        -- Returning nothing and just talking doesn't work, we need to review this.
        -- For now, let's return the text here.
        --talk("julia", "La puerta de SALIDA esta cerrada con llave.")
        return("Intento abrir la puerta, pero esta cerrada con llave.")
      end
    end,
  },
}

return room
