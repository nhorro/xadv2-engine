-- Global verb-handler fallback for Ingreso Urgente. Runs after a hotspot's room-
-- level Lua has had its chance; return a string to say something, or nothing
-- to let the engine emit its last-resort caption from `strings.defaults`.
--
-- Example:
--
--   look_at = function(target)
--     if target == "anything" then
--       return "Es interesante."
--     end
--   end

return {
}
