-- Scripted behavior for the lab_context_record_closeup close-up. Static data
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
    context_header = function()
      talk("player", "Registro contextual del hallazgo. La Matilde, Unidad 7B, Capa 3, Entierro 1.")
      talk("player", "Mismo individuo, mismo contexto. Bien. No estoy mezclando papeles al azar, que siempre es una posibilidad administrativa.")
    end,

    regular_stratigraphy = function()
      talk("player", "Sedimento limo arcilloso compacto. Estratigrafía regular y continua.")
      talk("player", "Sin bloques ni rocas sobre el individuo. Sin señales de colapso o derrumbe.")
      talk("player", "Eso debilita bastante una explicación por daño postdepositacional.")

      if not get_state("finding.no_collapse") then
        set_state("finding.no_collapse", true)
      end
    end,

    cranial_fragments = function()
      talk("player", "Fragmentos craneales junto al parietal derecho.")
      talk("player", "Eso los vincula espacialmente con la zona lesionada.")
      talk("player", "No prueba la causa por sí solo, pero evita que parezcan fragmentos sueltos sin relación.")
    end,

    lithic_object = function()
      talk("player", "Objeto lítico pesado recuperado en el sector.")
      talk("player", "Interesante, pero peligroso.")
      talk("player", "Puedo decir que es compatible con el contexto de un impacto. No puedo decir todavía que sea 'el arma'.")

      if not get_state("finding.heavy_lithic_object") then
        set_state("finding.heavy_lithic_object", true)
      end
    end,

    anatomical_position = function()
      talk("player", "Restos en posición anatómica, sin disturbios visibles.")
      talk("player", "Eso sugiere un entierro primario o, por lo menos, un contexto bastante poco alterado.")
      talk("player", "Si el cuerpo hubiera sido removido o arrastrado, esperaría otro tipo de desorden.")

      if not get_state("finding.primary_burial") then
        set_state("finding.primary_burial", true)
      end
    end,

    contextual_interpretation = function()
      talk("player", "La interpretación contextual dice que el contexto no respalda daño postdepositacional por derrumbe.")
      talk("player", "O sea: no alcanza con mirar el cráneo. El entorno también tiene que acompañar la hipótesis.")
      talk("player", "Schneider probablemente diría que un hueso sin contexto es una frase sacada de una conversación.")

      if not get_state("finding.no_collapse") then
        set_state("finding.no_collapse", true)
      end
    end,
  },
}
