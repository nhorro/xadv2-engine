local room = {}

function room.on_load()
    -- The door starts locked. Room state is per-room and persistent; a plain
    -- `local unlocked = false` here would be forgotten the moment you leave.
    if get_room_state("door.unlocked") == nil then
        set_room_state("door.unlocked", false)
    end
end

room.hotspots = {
    key = {
        look_at = function()
            return "A rusty key, left on the bench."
        end,
        pick_up = function()
            add_item("key")
            disable_hotspot("key")   -- it's in my pocket now; stop drawing it here
            return "I pocket the key."
        end,
    },

    bench = {
        look_at = function()
            if has_item("key") then
                return "A workbench. I already took the key."
            end
            return "A workbench. Something small and rusty is lying on it."
        end,
        push = function()
            return "Bolted to the floor."
        end,
    },

    door = {
        look_at = function()
            if get_room_state("door.unlocked") then
                return "The door to the yard, unlocked."
            end
            return "The door to the yard. The lock is old."
        end,
        open = function()
            if get_room_state("door.unlocked") then
                -- Second argument: the point in the target room to arrive at.
                change_room("yard", "from_workshop")
                return
            end
            return "Locked. I need a key."
        end,
    },
}

return room
