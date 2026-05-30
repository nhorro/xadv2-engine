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
  },
  -- --- handlers for the npc-bound hotspot template in exterior.yaml (issue #141) ---
  -- delivery_guy = {
  --   look_at = function() return "El repartidor, esperando junto a la camioneta." end,
  --   talk_to = function() talk("delivery_guy", "Traigo un paquete urgente.") end,
  -- },
}

return room
