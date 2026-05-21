-- Behavior for the study room. Static layout lives in study.yaml; this file says
-- what happens. M3 uses look_at handlers that return a caption (shown as speech).
local room = {}

function room.on_load()
  -- (no music asset yet; M4 wires audio + more hooks)
end

function room.on_unload() end

room.hotspots = {
  door = {
    look_at = function()
      return "La puerta de salida. Esta cerrada."
    end,
    open = function()
      return "No puedo abrirla todavia."
    end,
  },
}

return room
