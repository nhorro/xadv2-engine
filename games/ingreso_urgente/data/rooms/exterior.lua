-- Study room behavior. Static layout lives in study.yaml.
local room = {}

function room.on_load()
  -- play_music("music/thestudy.mp3") 
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
      --play_sound("sfx/door_open.ogg")
      change_room("hall", "from_exterior")      
    end,
  }
}

return room
