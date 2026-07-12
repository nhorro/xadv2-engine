local game = {}

game.fallbacks = {
    look_at = function(target)
        return "Nothing worth a second look."
    end,
    talk_to = function(target)
        return "It doesn't answer."
    end,
}

return game
