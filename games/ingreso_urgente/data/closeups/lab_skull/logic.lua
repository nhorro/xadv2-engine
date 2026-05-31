-- Scripted behavior for the lab_skull_closeup close-up. Static data
-- (background, hotspot polygons + names) lives in lab_skull.yml;
-- this file gives the hotspots behavior.
--
-- Hotspot handlers run as coroutine tasks, so talk() blocks line-to-line and the
-- player advances with a click. on_enter/on_exit are direct (non-blocking) setup
-- and cleanup, like a room's on_load/on_unload.

return {
  on_enter = function()
    -- No setup needed for now.
  end,

  on_exit = function()
    -- No cleanup needed for now.
  end,

  hotspots = {
    skull_parietal_injury = function()
      talk("player", "La lesión está concentrada en el parietal derecho.")
      talk("player", "No parece un daño distribuido al azar. Hay un punto focal bastante claro.")
      talk("player", "Eso, por sí solo, todavía no explica la causa. Pero sí me dice dónde mirar.")
    end,

    fracture_pattern = function()
      talk("player", "Las líneas de fractura se abren alrededor de la lesión.")
      talk("player", "Parecen irradiar desde un punto de impacto, más que responder a una rotura cualquiera.")

      if not get_state("finding.radial_fractures") then
        talk("player", "Ese patrón sí es defendible como fracturas radiales asociadas.")
        set_state("finding.radial_fractures", true)
      end
    end,

    skull_sinking = function()
      talk("player", "Acá se ve un hundimiento localizado, con fragmentos cerca del área afectada.")
      talk("player", "No es una deformación general del cráneo. Está bastante concentrada.")

      if not get_state("finding.perimortem_possible") then
        talk("player", "Esto no cierra el caso por sí solo, pero sí vuelve razonable considerar una lesión perimortem.")
        set_state("finding.perimortem_possible", true)
      end
    end,

    bones = function()
      talk("player", "El resto del material óseo está dispuesto en bandejas, ya separado para observación.")
      talk("player", "Hay bastante para clasificar, pero el cráneo claramente concentra el problema.")
      talk("player", "Schneider diría que conviene no enamorarse de los detalles si todavía no entendí el conjunto.")
    end,
  },
}