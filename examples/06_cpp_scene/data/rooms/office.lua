-- Note what this file does NOT do: it never mentions the notes screen's layout,
-- its font, or its scene class. It calls `discover_note`, a plain Lua function
-- the game's C++ installed at startup — indistinguishable, from here, from an
-- engine builtin like `add_item`.
local room = {}

function room.on_load()
    talk("player", "Someone left this office in a hurry.")
end

room.hotspots = {
    desk = {
        look_at = function()
            talk("player", "A drawer left open. Dust everywhere but one clean rectangle.")
            discover_note("desk")           -- ours (examples/06_cpp_scene/src)
        end,
        open = function()
            if has_note("ledger") then
                return "Nothing else in there."
            end
            talk("player", "A ledger. The last entry just... stops.")
            discover_note("ledger")
        end,
    },

    board = {
        look_at = function()
            talk("player", "Four pins. Three notes.")
            discover_note("board")
            if has_note("desk") and has_note("ledger") then
                talk("player", "Whoever cleared the desk took the fourth note with them.")
                -- Open our scene straight from a verb handler. The pause menu
                -- (Esc) reaches the same scene through the manifest's overlays.
                open_notes()
            end
        end,
    },
}

return room
