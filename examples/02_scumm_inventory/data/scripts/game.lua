-- Game-wide fallbacks. `use` takes both operands, so this is where a
-- "Use <anything> with <anything>" that nobody handled ends up.
local game = {}

game.fallbacks = {
    look_at = function(target)
        return "Nothing worth a second look."
    end,
    use = function(a, b)
        return "Those two don't go together."
    end,
}

return game
