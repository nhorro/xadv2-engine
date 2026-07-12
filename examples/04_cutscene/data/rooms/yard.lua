local room = {}

function room.on_load()
    talk("player", "So this is the yard they closed.")
end

room.hotspots = {
    gate = {
        look_at = function()
            return "The gate. Chained, exactly as the story said."
        end,
        open = function()
            if get_state("gate.remembered") then
                return "It won't move, and I already know why."
            end
            set_state("gate.remembered", true)
            -- Plays a cutscene mid-game. Unlike open_closeup (an overlay), this
            -- REPLACES the room, and the cutscene's own `on_finish` in game.yaml
            -- decides where to go next — here, straight back to room_view.
            -- Persistent state survives; the live room is unloaded and reloaded.
            start_cutscene("memory")
        end,
    },
}

return room
