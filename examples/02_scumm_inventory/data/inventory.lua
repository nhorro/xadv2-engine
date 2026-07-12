-- Item behaviour. A two-operand command whose FIRST operand is an item comes
-- here before it reaches the room ("inventory-first dispatch"): the `use`
-- handler below is what runs for "Use key with door", and its argument is the
-- id of whatever was picked as the second operand.
local inventory = {}

inventory.key = {
    look_at = function()
        return "Rusty, but the teeth look right."
    end,

    use = function(target)
        if target == "door" then
            if get_room_state("door.unlocked") then
                return "It's already unlocked."
            end
            set_room_state("door.unlocked", true)
            remove_item("key")
            return "The lock gives. The door is open."
        end
        -- Returning nothing here would fall through to the game-wide fallback;
        -- a specific refusal reads better.
        return "That's not what the key is for."
    end,
}

inventory.note = {
    look_at = function()
        return "\"...they took everything but the key.\""
    end,
}

return inventory
