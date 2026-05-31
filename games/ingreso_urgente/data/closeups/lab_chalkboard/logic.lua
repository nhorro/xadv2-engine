-- Scripted behavior for the lab_board_closeup close-up. Static data
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
    board_title = function()
      talk("player", "Sitio La Matilde. Unidad 7B, Capa 3, Sector Norte.")
      talk("player", "Schneider convirtió un corcho en algo bastante parecido a un argumento.")
      talk("player", "Todo está ahí, pero ordenado de un modo que sugiere que una respuesta vaga no va a sobrevivir mucho tiempo.")
    end,

    field_diary = function()
      talk("player", "Diario de campo: el entierro aparece durante la limpieza de la Unidad 7B, Capa 3.")
      talk("player", "Eso sitúa el hallazgo dentro de una secuencia de trabajo bastante normal.")
      talk("player", "No parece uno de esos contextos caóticos donde cualquier cosa pudo caerle encima al individuo y después desaparecer sin dejar rastro.")
    end,

    no_collapse_note = function()
      talk("player", "“Sin indicios de derrumbe”.")
      talk("player", "Estratigrafía regular. Sedimento compacto.")
      talk("player", "Si hubo daño, esta nota no ayuda a explicarlo como colapso del contexto.")

      if not get_state("finding.no_collapse") then
        set_state("finding.no_collapse", true)
      end
    end,

    primary_burial_note = function()
      talk("player", "“Hallazgo primario. En conexión anatómica. Sin disturbios visibles”.")
      talk("player", "Eso sugiere que el individuo estaba en posición relativamente intacta dentro de su entierro.")
      talk("player", "No suena a restos removidos, redepositados o revueltos después.")

      if not get_state("finding.primary_burial") then
        set_state("finding.primary_burial", true)
      end
    end,

    no_cut_marks_note = function()
      talk("player", "“Fragmentos junto al parietal derecho. Sin marcas de corte”.")
      talk("player", "Los fragmentos quedan asociados a la zona lesionada, y la ausencia de corte vuelve menos convincente una herramienta filosa.")
      talk("player", "No elimina todas las alternativas, pero sí acota bastante la discusión.")

      if not get_state("finding.no_cut_marks") then
        set_state("finding.no_cut_marks", true)
      end
    end,

    perimortem_note = function()
      talk("player", "“Lesión focalizada en región parietal derecha. Posible trauma perimortem”.")
      talk("player", "La palabra importante ahí es 'posible'.")
      talk("player", "No es una licencia para dramatizar. Es una hipótesis respaldada, pero todavía prudente.")

      if not get_state("finding.perimortem_possible") then
        set_state("finding.perimortem_possible", true)
      end
    end,

    site_map = function()
      talk("player", "Plano del sitio, posición del entierro y cuadrícula de excavación.")
      talk("player", "No me da una causa de muerte, pero sí algo igual de importante: contexto.")
      talk("player", "Si tengo que defender la interpretación, esto me recuerda que el problema no está sólo en el cráneo, sino en cómo encaja todo lo demás.")
    end,
  },
}