local room = {}

function room.on_load()
    talk("player", "That painting again. I've walked past it a hundred times.")
end

room.hotspots = {
    painting = {
        look_at = function()
            -- Arm a beat to run when the room is on top again. Register it BEFORE
            -- opening the close-up: the close-up's scope teardown cancels running
            -- tasks, but this callback is anchored and survives it.
            on_room_resume(function()
                if get_state("painting.face_unfinished")
                    and not get_room_state("said_unfinished") then
                    set_room_state("said_unfinished", true)
                    talk("player", "An unfinished face. I'd never noticed from back here.")
                end
            end)

            -- Push the close-up on top of the room. The room stays loaded: when
            -- the player backs out (Esc / right-click) we are exactly where we
            -- were, and anything the close-up learned is readable here.
            open_closeup("painting_closeup")
        end,
    },
}

return room
