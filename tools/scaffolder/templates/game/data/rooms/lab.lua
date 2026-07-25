-- Room behaviour for the starting room of {{title}}.
--
-- Engine lifecycle hooks (`on_load`, `on_unload`, ...) are top-level keys.
-- Hotspot handlers go under a `hotspots` sub-table, keyed by the hotspot id from
-- the room YAML. A hotspot handler placed at the top level is silently never
-- called — the engine only looks inside `hotspots`.
--
-- Example:
--
--   return {
--     on_load = function()
--       -- runs every time the room enters, after restore (if any)
--     end,
--
--     hotspots = {
--       sign = {
--         look_at = function() return "Un cartel oxidado." end,
--       },
--     },
--   }

return {
    hotspots = {
    },
}
