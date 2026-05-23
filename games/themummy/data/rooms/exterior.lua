local room = {}

function room.on_load()
  talk("julia", "Por fin, afuera.")
end

-- room.on_zone_enter = function(zone)
--   if zone == "to_hall" then
--     change_room("hall", "from_exterior")
--   end
-- end

room.hotspots = {
  door = {
    look_at = function()
      return "La puerta de ingreso al instituto."
    end,
    open = function()
      -- Note: we should be close
      change_room("hall", "from_exterior")
      return "Abro la puerta y entro al pasillo."
    end,
  },

}

return room
