-- =============================================================================
--  DIALOG — Dra. Schneider / Puzzle de evidencia arqueológica
-- =============================================================================
--
--  Archivo sugerido: dialogs/dra_schneider.lua
--
--  La Dra. Schneider es el NPC. Julia es la protagonista y habla mediante las
--  opciones del jugador.
--
--  Este diálogo no permite resolver el caso eligiendo directamente una hipótesis.
--  Primero Julia debe enunciar observaciones, luego descartar hipótesis débiles,
--  y recién entonces puede formular una conclusión preliminar.
-- =============================================================================

local function flag(name)
  return get_state(name) == true
end

local function set_flag(name)
  set_state(name, true)
end

local function attempts()
  return get_state("case.schneider_attempts") or 0
end

local function add_attempt()
  set_state("case.schneider_attempts", attempts() + 1)
end

local function has_basic_observations()
  return flag("argument.fracture_pattern_stated")
     and flag("argument.no_cut_marks_stated")
     and flag("argument.no_collapse_stated")
end

local function has_discarded_alternatives()
  return flag("argument.discarded_blade")
     and flag("argument.discarded_collapse")
end

local function can_state_final_hypothesis()
  return has_basic_observations()
     and has_discarded_alternatives()
     and flag("argument.perimortem_stated")
end

return {

  start = "intro",

  on_enter = function()
    set_flag("schneider.met")
  end,

  on_exit = function()
  end,

  -- ---------------------------------------------------------------------------
  -- Intro
  -- ---------------------------------------------------------------------------

  intro = {
    npc = {
      "A ver, Serrategui.",
      "No me diga todavía qué cree que pasó.",
      "Primero ordenemos la evidencia. Después, si sobrevive al orden, hablamos de hipótesis.",
    },
    to = "hub",
  },

  -- ---------------------------------------------------------------------------
  -- Hub principal
  -- ---------------------------------------------------------------------------

  hub = {
    npc = "Empiece por lo observable. ¿Qué tenemos?",

    options = {

      {
        "El cráneo muestra una lesión focal con fracturas radiales.",
        when = function()
          return flag("finding.radial_fractures")
             and not flag("argument.fracture_pattern_stated")
        end,
        run = function()
          set_flag("argument.fracture_pattern_stated")
        end,
        to = "fracture_pattern",
      },

      {
        "No se ven marcas de corte en la superficie ósea.",
        when = function()
          return flag("finding.no_cut_marks")
             and not flag("argument.no_cut_marks_stated")
        end,
        run = function()
          set_flag("argument.no_cut_marks_stated")
        end,
        to = "no_cut_marks",
      },

      {
        "El registro contextual no muestra derrumbe ni compresión sedimentaria clara.",
        when = function()
          return flag("finding.no_collapse")
             and not flag("argument.no_collapse_stated")
        end,
        run = function()
          set_flag("argument.no_collapse_stated")
        end,
        to = "no_collapse",
      },

      {
        "El entierro parece primario, sin disturbios visibles.",
        when = function()
          return flag("finding.primary_burial")
             and not flag("argument.primary_burial_stated")
        end,
        run = function()
          set_flag("argument.primary_burial_stated")
        end,
        to = "primary_burial",
      },

      {
        "Hay un objeto lítico pesado recuperado en el mismo sector.",
        when = function()
          return flag("finding.heavy_lithic_object")
             and not flag("argument.heavy_object_stated")
        end,
        run = function()
          set_flag("argument.heavy_object_stated")
        end,
        to = "heavy_object",
      },

      {
        "La ficha osteológica habla de posible lesión perimortem.",
        when = function()
          return flag("finding.perimortem_possible")
             and not flag("argument.perimortem_stated")
        end,
        run = function()
          set_flag("argument.perimortem_stated")
        end,
        to = "perimortem",
      },

      {
        "Podría haber sido un ataque con herramienta filosa.",
        when = function()
          return flag("argument.no_cut_marks_stated")
             and not flag("argument.discarded_blade")
        end,
        run = function()
          add_attempt()
          set_flag("argument.discarded_blade")
        end,
        to = "wrong_blade",
      },

      {
        "Podría ser daño postdepositacional por derrumbe.",
        when = function()
          return flag("argument.no_collapse_stated")
             and not flag("argument.discarded_collapse")
        end,
        run = function()
          add_attempt()
          set_flag("argument.discarded_collapse")
        end,
        to = "wrong_collapse",
      },

      {
        "Podría haber sido una caída accidental.",
        when = function()
          return has_basic_observations()
             and not flag("argument.discussed_fall")
        end,
        run = function()
          add_attempt()
          set_flag("argument.discussed_fall")
        end,
        to = "possible_fall",
      },

      {
        "El patrón más consistente es trauma contundente perimortem.",
        when = function()
          return can_state_final_hypothesis()
        end,
        to = "correct_hypothesis",
      },

      {
        "Creo que todavía me falta revisar evidencia.",
        to = "not_ready",
      },

      {
        "Después seguimos.",
        to = END,
      },
    },
  },

  -- ---------------------------------------------------------------------------
  -- Observaciones
  -- ---------------------------------------------------------------------------

  fracture_pattern = {
    npc = {
      "Bien. Eso es una observación útil.",
      "Una fractura radial no prueba por sí sola la causa, pero sí sugiere que hubo un foco de energía.",
      "No me diga todavía quién, ni con qué. Primero mecanismo.",
    },
    to = "hub",
  },

  no_cut_marks = {
    npc = {
      "Correcto.",
      "La ausencia de cortes no demuestra un golpe, pero debilita bastante la hipótesis de una herramienta filosa.",
      "No confunda descartar una posibilidad con probar otra.",
    },
    to = "hub",
  },

  no_collapse = {
    npc = {
      "Eso importa más de lo que parece.",
      "Si el contexto no muestra derrumbe, bloques ni compresión general, el daño postdepositacional pierde fuerza.",
      "La tafonomía no se invoca para tapar cualquier cosa que no entendamos.",
    },
    to = "hub",
  },

  primary_burial = {
    npc = {
      "Bien observado.",
      "Si el entierro está en posición anatómica y sin disturbios, al menos no parece una mezcla posterior de restos.",
      "Eso no resuelve el trauma, pero mejora la confiabilidad del conjunto.",
    },
    to = "hub",
  },

  heavy_object = {
    npc = {
      "Cuidado con esa pista.",
      "Un objeto pesado en el sector puede ser relevante, pero también puede ser asociación contextual sin relación causal.",
      "No lo use como arma hasta que la evidencia ósea lo justifique.",
    },
    to = "hub",
  },

  perimortem = {
    npc = {
      "Esa palabra conviene usarla con prudencia.",
      "Perimortem no significa automáticamente asesinato.",
      "Significa que el patrón de fractura es compatible con hueso fresco, no con una rotura seca mucho más tardía.",
    },
    to = "hub",
  },

  -- ---------------------------------------------------------------------------
  -- Hipótesis alternativas
  -- ---------------------------------------------------------------------------

  wrong_blade = {
    npc = {
      "No me convence.",
      "Para sostener herramienta filosa esperaría cortes lineales, bordes más limpios o marcas repetidas.",
      "Lo que tenemos apunta más a impacto que a filo.",
    },
    to = "hub",
  },

  wrong_collapse = {
    npc = {
      "Podría considerarse, pero el contexto no acompaña.",
      "El registro habla de estratigrafía regular, sin bloques sobre el individuo y sin señales claras de colapso.",
      "Si fue derrumbe, tendría que explicar por qué sólo el cráneo muestra ese patrón focal.",
    },
    to = "hub",
  },

  possible_fall = {
    npc = {
      "Es una alternativa razonable, pero todavía floja.",
      "Una caída puede producir trauma craneal, sí.",
      "Pero acá la lesión es focal, el contexto no muestra desorden y no tenemos otras fracturas que refuercen una caída desde altura.",
      "No la descarte por completo, pero no es la hipótesis más económica.",
    },
    to = "hub",
  },

  -- ---------------------------------------------------------------------------
  -- Falta evidencia / pistas adaptativas
  -- ---------------------------------------------------------------------------

  not_ready = {
    npc = {
      "Eso es una respuesta aceptable, aunque poco heroica.",
      "Revise el cráneo, el corcho, la ficha osteológica y el registro contextual.",
      "Después vuelva con observaciones, no con corazonadas.",
    },
    to = END,
  },

  hint_after_attempts = {
    npc = {
      "Serrategui, está probando respuestas.",
      "Ordene el razonamiento: lesión, mecanismo, contexto, descarte.",
      "Si una hipótesis no explica esas cuatro cosas, todavía no es una hipótesis. Es decoración.",
    },
    to = "hub",
  },

  -- ---------------------------------------------------------------------------
  -- Resolución
  -- ---------------------------------------------------------------------------

  correct_hypothesis = {
    npc = {
      "Ahora sí.",
      "Lesión focal, fracturas radiales, hundimiento localizado, sin marcas de corte y sin contexto de derrumbe.",
      "La hipótesis más consistente es trauma contundente perimortem.",
      "Eso no dice quién lo hizo. Ni siquiera dice con certeza que haya sido intencional.",
      "Pero sí nos permite descartar las explicaciones más cómodas.",
      "Y en arqueología, las explicaciones cómodas suelen ser las primeras que hay que incomodar.",
    },
    run = function()
      set_flag("case.blunt_trauma_supported")
      set_flag("case.report_ready")
    end,
    to = "after_solution",
  },

  after_solution = {
    npc = "Prepare una conclusión preliminar. Corta, sobria y sin entusiasmo detectivesco.",
    options = {
      {
        "¿Puedo escribir que fue un golpe con objeto romo?",
        to = "wording_careful",
      },
      {
        "¿Y el objeto lítico pesado?",
        when = function()
          return flag("argument.heavy_object_stated")
        end,
        to = "lithic_object_final",
      },
      {
        "Entendido. Redacto el informe.",
        run = function()
          set_flag("case.schneider_dialog_done")
        end,
        to = END,
      },
    },
  },

  wording_careful = {
    npc = {
      "Puede escribir: patrón compatible con trauma contundente perimortem.",
      "No escriba causa de muerte.",
      "No escriba arma homicida.",
      "No escriba nada que después tenga que defender con cara de estatua.",
    },
    to = "after_solution",
  },

  lithic_object_final = {
    npc = {
      "Menciónelo como asociación contextual, no como arma.",
      "Si más adelante aparece residuo, correspondencia de forma o una marca compatible, hablamos.",
      "Por ahora, que el objeto sea pesado no lo convierte en culpable. A varios colegas les pasa lo mismo.",
    },
    to = "after_solution",
  },
}