local inventory = {}

inventory.ticket = {
    look_at = function()
        return "Today's date. Admit one."
    end,
    -- "Give ticket to curator" reaches the room's `curator.give` handler; the
    -- dialog offers the same exchange as a conversation option. Two routes to
    -- the same beat is normal — both end at the same state flag.
}

return inventory
