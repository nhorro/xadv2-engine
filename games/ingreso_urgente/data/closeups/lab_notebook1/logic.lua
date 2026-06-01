-- Scripted behavior for the lab_osteology_closeup close-up. Static data
-- (background, hotspot polygons + names) lives in the YAML file;
-- this file gives the hotspots behavior.

return {
  on_enter = function()
    -- No setup needed for now.
  end,

  on_exit = function()
    -- No cleanup needed for now.
  end,

  hotspots = {
    osteology_header = function()
      talk("player", "Sitio La Matilde. Unidad 7B. Capa 3. Entierro 1.")
      talk("player", "Al menos el documento sabe dónde está parado. Eso ya es más de lo que puedo decir de mí hace cinco minutos.")
    end,

    trauma_observations = function()
      talk("player", "Lesión focal en parietal derecho, fracturas radiales, hundimiento localizado y fragmentos próximos.")
      talk("player", "La ficha no está contando una historia: está armando una cadena de observaciones.")
      talk("player", "Si voy a defender la hipótesis, tengo que poder relacionar esas piezas sin inventar una novela.")

      if not get_state("finding.radial_fractures") then
        set_state("finding.radial_fractures", true)
      end
      discover_evidence("radial_fractures")

      if not get_state("finding.perimortem_possible") then
        talk("player", "El conjunto es compatible con una lesión ocurrida cerca del momento de la muerte, pero todavía hay que cruzarlo con el contexto.")
        set_state("finding.perimortem_possible", true)
      end
      discover_evidence("perimortem_possible")
    end,

    no_cut_marks = function()
      talk("player", "Sin marcas de corte visibles.")
      talk("player", "Eso debilita una explicación con herramienta filosa.")
      talk("player", "Debilita, no elimina. Schneider me arrancaría la cabeza si convierto ausencia de evidencia en certeza absoluta.")

      if not get_state("finding.no_cut_marks") then
        set_state("finding.no_cut_marks", true)
      end
      discover_evidence("no_cut_marks")
    end,

    no_compression = function()
      talk("player", "Sin evidencia clara de compresión sedimentaria general.")
      talk("player", "Entonces el daño no parece parte de una deformación distribuida sobre todo el cráneo.")
      talk("player", "Otra vez: no prueba el impacto por sí solo, pero vuelve menos cómoda la explicación postdepositacional.")

      if not get_state("finding.no_collapse") then
        set_state("finding.no_collapse", true)
      end
      discover_evidence("no_collapse")
    end,

    preliminary_conclusion = function()
      talk("player", "Conclusión preliminar: patrón compatible con trauma contundente perimortem.")
      talk("player", "La frase ya está escrita. El problema es si puedo defenderla sin repetirla como loro con guardapolvo.")
      talk("player", "Trauma contundente no significa automáticamente asesinato. Perimortem tampoco significa automáticamente intención.")
      talk("player", "Eso seguro Schneider me lo va a hacer decir en voz alta.")
    end,
  },
}
