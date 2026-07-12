-- The room's BEHAVIOUR: what happens. The layout it talks about lives in
-- study.yaml — this file never mentions a coordinate.
--
-- A verb handler that returns a string makes the player say it. Returning
-- nothing falls through: first to scripts/game.lua's fallbacks, then to the
-- engine's last-resort caption from strings/en.yaml `defaults`.
local room = {}

-- Lifecycle. on_load runs every time the room is entered (after a save is
-- restored, if there was one); on_unload just before leaving it.
function room.on_load()
    -- Speech is queued, so this line plays as the room opens.
    talk("player", "A quiet study. Someone left in a hurry.")
end

function room.on_unload() end

room.hotspots = {
    window = {
        look_at = function()
            return "Night outside. My own face, looking back."
        end,
        open = function()
            -- Room state is persistent: it survives leaving the room and is
            -- written to the save file. Lua locals are NOT — anything the game
            -- must remember goes through set_state / set_room_state.
            if get_room_state("window.open") then
                return "It's already open."
            end
            set_room_state("window.open", true)
            return "I push the window open. Cold air comes in."
        end,
    },

    bench = {
        look_at = function()
            return "A workbench, swept clean. Not a single tool left."
        end,
        push = function()
            return "It doesn't budge. Bolted to the floor."
        end,
    },

    poster = {
        look_at = function()
            -- talk() blocks inside a handler (handlers run as coroutines), so
            -- several lines play one after another, each waiting for a click.
            talk("player", "A faded poster. An exhibition that closed years ago.")
            talk("player", "Someone drew a circle around one of the exhibits.")
        end,
        pull = function()
            return "I'd rather not tear it."
        end,
    },

    door = {
        look_at = function()
            return "The way out. Locked, of course."
        end,
        open = function()
            return "Locked. The next example gives me a key."
        end,
    },
}

return room
