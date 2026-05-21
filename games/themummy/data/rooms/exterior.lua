local room = {}

function room.on_load()
  talk("julia", "Por fin, afuera.")
end

room.on_zone_enter = function(zone)
  if zone == "to_hall" then
    change_room("hall", "from_exterior")
  end
end

room.hotspots = {}

return room
