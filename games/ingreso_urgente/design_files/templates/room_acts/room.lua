-- TEMPLATE: data/rooms/<room>.lua  (rename <room> and the act paths/ids).
-- The slim entry script the engine loads by room id. Static layout is in
-- <room>.yaml; per-act behaviour lives in the act modules below.

local flow = include("rooms/_act_flow.lua")

-- Name your acts to match the "<room>.cfg" integer values (see scripts/game.lua).
local CFG_FIRST = 1
-- local CFG_SECOND = 2
-- local CFG_THIRD  = 3

-- 1-based, indexed by "<room>.cfg".
local acts = {
    include("rooms/<room>/_act1.lua"),
    -- include("rooms/<room>/_act2.lua"),
    -- include("rooms/<room>/_act3.lua"),
}

local room = {}

function room.on_load()
    flow.enter("<room>", acts)
end

function room.on_unload() end

--------------------------------------------------------------------------------
-- Transitions between acts (BLOCKING). Make them global if cutscenes / other
-- rooms drive them. Advance "<room>.cfg", then rebuild presence with
-- flow.configure (INSTANT).
--------------------------------------------------------------------------------
-- function some_beat()
--     block_input()
--     -- talk(...) / wait(...) / move_to(...)
--     set_state("<room>.cfg", CFG_SECOND)
--     flow.configure("<room>", acts)
--     unblock_input()
-- end

--------------------------------------------------------------------------------
-- Hotspots. Constant ones go here directly. For a hotspot that differs per act,
-- dispatch to the active act module, e.g.:
--   local function act() return acts[get_state("<room>.cfg") or 1] end
--   room.hotspots = { thing = { use = function() return act().hotspots.thing.use() end } }
--------------------------------------------------------------------------------
room.hotspots = {
    -- door = {
    --     look_at = function() return "..." end,
    --     use = function() change_room("<other>", "<entry>") end,
    -- },
}

return room
