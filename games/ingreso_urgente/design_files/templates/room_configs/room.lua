-- TEMPLATE: data/rooms/<room>.lua  (rename <room> and the config paths/ids).
-- The slim entry script the engine loads by room id. Static layout is in
-- <room>.yaml; per-configuration setup lives in the config modules below.

local flow = include("rooms/_room_flow.lua")

-- Name your configs to match the "<room>.cfg" integer values (see scripts/game.lua).
local CFG_FIRST = 1
-- local CFG_SECOND = 2
-- local CFG_THIRD  = 3

-- 1-based, indexed by "<room>.cfg". Group the files by act in subdirs.
local configs = {
    include("rooms/<room>/act1/<role>.lua"),
    -- include("rooms/<room>/act1/<role2>.lua"),
    -- include("rooms/<room>/act2/<role>.lua"),   -- later acts append here
}

local room = {}

function room.on_load()
    flow.enter("<room>", configs)
end

function room.on_unload() end

--------------------------------------------------------------------------------
-- Transitions between configs (BLOCKING). Make them global if cutscenes / other
-- rooms drive them. Advance "<room>.cfg", then rebuild presence with
-- flow.configure (INSTANT).
--------------------------------------------------------------------------------
-- function some_beat()
--     block_input()
--     -- talk(...) / wait(...) / move_to(...)
--     set_state("<room>.cfg", CFG_SECOND)
--     flow.configure("<room>", configs)
--     unblock_input()
-- end

--------------------------------------------------------------------------------
-- Hotspots. Keep them centralized here, branching on state (cfg / flags) when a
-- hotspot differs per config — only the per-config *setup* moves to the modules.
--------------------------------------------------------------------------------
room.hotspots = {
    -- door = {
    --     look_at = function() return "..." end,
    --     use = function() change_room("<other>", "<entry>") end,
    -- },
}

return room
