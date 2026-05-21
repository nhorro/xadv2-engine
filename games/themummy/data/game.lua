-- Global game logic: shared fallbacks for commands a hotspot/item does not handle.
local game = {}

game.fallbacks = {
  look_at = function(target)
    return "No veo nada especial."
  end,
  use = function(a, b)
    return "No funciona."
  end,
}

return game
