-- Study room behavior. Static layout lives in study.yaml.
local room = {}

function room.on_load()
  play_music("music/thestudy.mp3") 
end
function room.on_unload() end

-- room.on_zone_enter = function(zone)
--   if zone == "to_hall" then
--     change_room("hall", "from_study")
--   end
-- end

room.hotspots = {
  skull = {
    look_at = function()
      return "Un cráneo humano."
    end,
    talk_to = function()
      start_dialog("skull")
    end,
  },
  door = {
    look_at = function()
      return "La puerta da al pasillo del instituto."
    end,
    open = function()
      change_room("hall", "from_study")
      return "Abro la puerta y entro al pasillo."
    end,
  },
  notebook = {
    look_at = function()
      return "Un cuaderno de notas de campo."
    end,
    pick_up = function()
      add_item("notebook")
      disable_hotspot("notebook")
      return "Agarro el cuaderno."
    end,
  },
  stan = {
    look_at = function()
      return "Es Stan, el portero del instituto."
    end,
    talk_to = function()
      start_dialog("stan")
    end,
  },
}

return room
