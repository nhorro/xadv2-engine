-- Game-wide fallbacks: what to say when a hotspot's own handler didn't take the
-- command. Runs after the room's Lua has had its chance, before the engine's
-- last-resort captions in strings/en.yaml `defaults`.
local game = {}

game.fallbacks = {
    look_at = function(target)
        return "Nothing worth a second look."
    end,
}

return game
