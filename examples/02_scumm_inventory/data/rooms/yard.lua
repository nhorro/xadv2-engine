local room = {}

function room.on_load()
    talk("player", "Outside at last.")
end

-- Fired when the player's feet enter a zone declared in yard.yaml.
function room.on_zone_enter(zone)
    if zone == "back_inside" then
        change_room("workshop", "from_yard")
    end
end

room.hotspots = {
    crate = {
        look_at = function()
            return "A crate, lid loose."
        end,
        open = function()
            if get_room_state("crate.open") then
                return "Empty now."
            end
            set_room_state("crate.open", true)
            add_item("note")
            return "Inside: a torn note. I take it."
        end,
        push = function()
            return "Heavier than it looks."
        end,
    },

    gate = {
        look_at = function()
            return "The gate out. Chained shut."
        end,
        open = function()
            return "Chained. Whatever opens it isn't in this example."
        end,
        pull = function()
            return "The chain holds."
        end,
    },
}

return room
