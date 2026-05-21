-- Inventory item behavior. Two-operand commands whose first operand is an item
-- are routed here first (inventory-first dispatch).
local inventory = {}

inventory.key = {
  look_at = function()
    return "Una llave oxidada."
  end,
  use = function(target)
    if target == "salida" then
      if get_room_state("salida.open") then
        return "Ya esta abierta."
      end
      remove_item("key")
      set_room_state("salida.open", true)
      return "La llave gira. La puerta de SALIDA se abre."
    end
    return "No funciona ahi."
  end,
}

inventory.notebook = {
  look_at = function()
    return "Mis notas de campo."
  end,
}

return inventory
