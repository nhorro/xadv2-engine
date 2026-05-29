-- Room behaviour for the starting room of {{title}}. Each top-level entry
-- is either an engine lifecycle hook (`on_load`, `on_unload`, ...) or a
-- hotspot id whose value is a table of verb handlers.
--
-- Example:
--
--   on_load = function()
--     -- runs every time the room enters, after restore (if any)
--   end,
--
--   sign = {
--     look_at = function() return "Un cartel oxidado." end,
--   },

return {
}
