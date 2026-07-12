-- A dialog tree. Each key is a node; `start` names the first one.
--
-- A node either speaks (`npc`: one line or a list of lines) and jumps (`to`), or
-- offers `options`. The canonical shape is "greet once, then hub": `intro`
-- speaks and falls into `hub`, a pure-options node that every branch returns to,
-- so the greeting doesn't replay on every lap.
--
-- Option fields:
--   when  — a predicate; the option is only shown when it returns true
--   once  — the option disappears after it has been taken
--   run   — a side effect (state, inventory) that fires when it is chosen
--   to    — the next node, or END to close the conversation
return {
    start = "intro",

    intro = {
        npc = "We close in ten minutes.",
        to = "hub",
    },

    hub = {
        options = {
            { "What is that painting?", to = "painting" },

            {
                "Who painted it?",
                -- Only offered once you've asked what it is.
                when = function() return get_state("curator.told_about_painting") == true end,
                to = "painter",
            },

            {
                "Here — you dropped this ticket.",
                when = function() return has_item("ticket") end,
                once = true,
                run = function()
                    remove_item("ticket")
                    set_state("curator.has_ticket", true)
                end,
                to = "ticket",
            },

            { "Nothing. Good night.", to = END },
        },
    },

    painting = {
        npc = {
            "It isn't finished.",
            "He died before he could paint the face. We hang it anyway.",
        },
        run = function()
            -- Global state: survives room changes and save/load, unlike a Lua local.
            set_state("curator.told_about_painting", true)
        end,
        to = "hub",
    },

    painter = {
        npc = "A name nobody remembers. That's rather the point of this room.",
        to = "hub",
    },

    ticket = {
        npc = "So I did. Thank you.",
        to = "hub",
    },
}
