-- The close-up's behaviour. Hotspots here have no verbs: clicking one just runs
-- its function. (There is no verb grid in a close-up — you are looking, not
-- acting.)
--
-- Handlers run as coroutine tasks, so talk() BLOCKS: the lines below play one
-- after another, each waiting for the player to click. on_enter / on_exit are
-- plain setup/teardown and do not block.
return {
    on_enter = function()
        talk("player", "Close enough to see the brush marks.")
    end,

    on_exit = function()
        -- Nothing to clean up here. State set below persists: it lives in the
        -- engine's GameState, not in this file.
    end,

    hotspots = {
        face = function()
            talk("player", "The face was never painted. Just primed canvas.")
            talk("player", "So the curator was telling the truth.")
            -- Close-up findings are how a close-up feeds the rest of the game: a
            -- room, a dialog option, or another close-up can read this back.
            set_state("painting.face_unfinished", true)
        end,

        crack = function()
            talk("player", "A crack runs the whole height of the varnish.")
            if get_state("painting.face_unfinished") then
                talk("player", "It stops exactly where the paint stops.")
            end
        end,

        signature = function()
            talk("player", "\"H. 1911\". Signed, then. Whoever H. was.")
            set_state("painting.signature_read", true)
        end,
    },
}
