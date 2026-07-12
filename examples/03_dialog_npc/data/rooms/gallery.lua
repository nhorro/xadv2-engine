local room = {}

function room.on_load()
    -- NPC presence is scripted, not baked into the YAML: spawn it where you want
    -- it, facing where you want. despawn_npc("curator") takes it away again.
    spawn_npc("curator", "curator_spot", "left")

    -- avatar(id) is a handle: it stores the id only and re-resolves it on every
    -- call, so it stays valid across room reloads.
    avatar("curator"):face("left")

    -- Give the player something to hand over, exactly once: on_load runs on every
    -- entry, so the guard is what makes it a one-time event.
    if not get_room_state("visited") then
        set_room_state("visited", true)
        add_item("ticket")
    end
end

function room.on_unload() end

room.hotspots = {
    curator = {
        look_at = function()
            return "The curator. She has been watching me since I came in."
        end,
        talk_to = function()
            -- Hands the scene over to the dialog runtime: dialogs/curator.lua
            -- drives the conversation until it hits END.
            start_dialog("curator")
        end,
        give = function(item)
            if item == "ticket" then
                return "She waves it away. \"Keep it. You'll need it to get out.\""
            end
        end,
    },

    painting = {
        look_at = function()
            talk("player", "A figure in blue. No plaque, no title.")
            if get_state("curator.told_about_painting") then
                talk("player", "The curator says it isn't finished. It looks finished to me.")
            end
        end,
    },
}

return room
