-- Study room behavior. Static layout lives in study.yaml.
local room = {}

function room.on_load() end
function room.on_unload() end

room.on_zone_enter = function(zone)
  if zone == "to_hall" then
    change_room("hall", "from_study")
  end
end

room.hotspots = {
  door = {
    look_at = function()
      return "La puerta da al pasillo del instituto."
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
}

return room
