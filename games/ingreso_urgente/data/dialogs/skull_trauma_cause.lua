-- =============================================================================
--  DIALOG — Dra. Schneider / Puzzle de defensa de evidencia arqueológica
-- =============================================================================
--
--  La Dra. Schneider es el NPC. Julia es la protagonista y habla mediante las
--  opciones del jugador.
--
--  Premisa del diálogo:
--  La conclusión preliminar ya existe en la ficha. Schneider no quiere que Julia
--  la repita: quiere comprobar si puede defenderla, distinguir observación de
--  inferencia y no afirmar más de lo que la evidencia permite.
--
--  ---------------------------------------------------------------------------
--  ESTE DIÁLOGO USA `dialog { ... }` Y `topic`
--  ---------------------------------------------------------------------------
--  El puzzle es un menú central (el "hub") del que cuelgan muchas afirmaciones
--  que Julia puede sostener. Cada afirmación aparece sólo cuando corresponde, se
--  dice una sola vez, y devuelve la conversación al hub. Escribir eso a mano es
--  repetir, por cada tema, una opción (con `when`/`run`/`to`) MÁS un nodo de
--  respuesta. `topic` une las dos mitades en una sola declaración:
--
--      topic "id" {
--        requires = "finding.algo",  -- aparece cuando este estado es true
--        after    = "otro_topic",    -- ...o cuando ya se dijo otro topic
--        player   = "Lo que dice Julia.",
--        npc      = { "Lo que responde Schneider." },
--        run      = function() ... end,   -- opcional, efecto extra al decirlo
--      }
--
--  Se listan en `topics = { ... }` dentro del nodo hub. El motor las expande a la
--  forma estándar (opción + nodo) antes de jugar. `requires` puede ser el nombre
--  de un estado (string) o una función-predicado; `after` espera el id de otro
--  topic (o una lista). Cada topic se ofrece una sola vez (`uttered(id)` queda en
--  true al decirlo) y vuelve al hub. Las opciones "trampa" y la resolución siguen
--  siendo opciones/nodos normales: topics y opciones crudas conviven en un mismo
--  nodo. Por eso este archivo está envuelto en `return dialog { ... }`.
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

-- Predicados del razonamiento, ahora en términos de los topics ya dichos.
local function has_basic_observations()
  return uttered("fracture_pattern")
     and uttered("no_cut_marks")
     and uttered("no_collapse")
end

local function has_discarded_alternatives()
  return uttered("discard_blade")
     and uttered("discard_collapse")
end

local function can_state_final_hypothesis()
  return has_basic_observations()
     and has_discarded_alternatives()
     and uttered("perimortem")
     and get_hypothesis_conclusion("matilde_trauma") == "blunt_perimortem"
     and is_hypothesis_complete("matilde_trauma")
end

return dialog {

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
      "La ficha ya dice 'patrón compatible con trauma contundente perimortem'.",
      "No necesito que la lea en voz alta.",
      "Necesito saber si entiende por qué esa frase es defendible.",
      "Y, sobre todo, qué no autoriza a afirmar.",
    },
    to = "hub",
  },

  -- ---------------------------------------------------------------------------
  -- Hub principal
  -- ---------------------------------------------------------------------------

  hub = {
    npc = "Empiece por lo observable. ¿Qué puede sostener?",

    -- Las afirmaciones del puzzle. Cada una se ofrece cuando su `requires`/`after`
    -- se cumple, se dice una sola vez, y vuelve al hub.
    topics = {

      topic "fracture_pattern" {
        requires = "finding.radial_fractures",
        player = "El cráneo muestra una lesión focal con fracturas radiales.",
        npc = {
          "Bien. Eso sí es una observación útil.",
          "Una lesión focal con fracturas radiales sugiere un punto de impacto.",
          "Todavía no me diga quién, ni con qué, ni por qué.",
          "Primero mecanismo. Después, si la evidencia lo permite, interpretación.",
        },
      },

      topic "no_cut_marks" {
        requires = "finding.no_cut_marks",
        player = "No se ven marcas de corte en la superficie ósea.",
        npc = {
          "Correcto.",
          "La ausencia de marcas de corte debilita una explicación con herramienta filosa.",
          "Debilita. No elimina por decreto.",
          "No confunda descartar una posibilidad con probar otra.",
        },
      },

      topic "no_collapse" {
        requires = "finding.no_collapse",
        player = "El registro contextual no muestra derrumbe ni compresión sedimentaria clara.",
        npc = {
          "Eso importa más de lo que parece.",
          "Si el contexto no muestra derrumbe, bloques ni compresión general, el daño postdepositacional pierde fuerza.",
          "La tafonomía no se invoca para tapar cualquier cosa que no entendamos.",
        },
      },

      topic "primary_burial" {
        requires = "finding.primary_burial",
        player = "El entierro parece primario, sin disturbios visibles.",
        npc = {
          "Bien observado.",
          "Si el entierro está en posición anatómica y sin disturbios visibles, el conjunto es más confiable.",
          "No resuelve el trauma, pero reduce la probabilidad de que estemos leyendo una mezcla posterior de restos.",
        },
      },

      topic "heavy_object" {
        requires = "finding.heavy_lithic_object",
        player = "Hay un objeto lítico pesado recuperado en el mismo sector.",
        npc = {
          "Cuidado con esa pista.",
          "Un objeto pesado en el sector puede ser relevante, pero también puede ser sólo una asociación contextual.",
          "Que un objeto sea pesado no lo convierte en arma. A varios colegas les pasa lo mismo.",
        },
      },

      topic "perimortem" {
        requires = "finding.perimortem_possible",
        player = "La lesión podría ser perimortem, no una rotura seca tardía.",
        npc = {
          "Esa palabra conviene usarla con prudencia.",
          "Perimortem no significa automáticamente asesinato.",
          "Significa que el patrón es compatible con hueso fresco, no con una rotura seca mucho más tardía.",
          "Es una ventana temporal, no una novela policial.",
        },
      },

      -- `after` en vez de `requires`: estos descartes sólo tienen sentido una vez
      -- que Julia ya hizo la observación correspondiente.
      topic "discard_blade" {
        after = "no_cut_marks",
        player = "Una herramienta filosa no explica bien el patrón.",
        npc = {
          "De acuerdo.",
          "Para sostener herramienta filosa esperaríamos cortes lineales, bordes más limpios o marcas repetidas.",
          "Lo que tenemos apunta mejor a impacto que a filo.",
        },
      },

      topic "discard_collapse" {
        after = "no_collapse",
        player = "El derrumbe o la compresión sedimentaria no parecen suficientes.",
        npc = {
          "De acuerdo.",
          "El registro habla de estratigrafía regular, sin bloques sobre el individuo y sin señales claras de colapso.",
          "Si fuera derrumbe, tendría que explicar por qué el daño aparece tan focalizado en el cráneo.",
        },
      },

      -- `requires` puede ser un predicado: esta alternativa sólo se ofrece cuando
      -- las tres observaciones básicas ya están sobre la mesa.
      topic "possible_fall" {
        requires = has_basic_observations,
        player = "Podría haber sido una caída accidental.",
        npc = {
          "Es una alternativa que puede considerarse.",
          "Una caída puede producir trauma craneal, sí.",
          "Pero acá la lesión es focal, el contexto no muestra desorden y no tenemos otras fracturas que refuercen una caída desde altura.",
          "No la descarte por orgullo teórico. Déjela como menos consistente con el conjunto.",
        },
      },
    },

    -- Opciones crudas: las "trampas" (con su propio conteo de intentos) y la
    -- resolución, que no encajan en el patrón de un topic.
    options = {

      {
        "Entonces fue asesinado.",
        when = function()
          return has_basic_observations()
             and not flag("argument.murder_overstated")
        end,
        run = function()
          add_attempt()
          set_flag("argument.murder_overstated")
        end,
        to = "murder_overstated",
      },

      {
        "El objeto lítico podría ser el arma.",
        when = function()
          return uttered("heavy_object")
             and not flag("argument.weapon_overstated")
        end,
        run = function()
          add_attempt()
          set_flag("argument.weapon_overstated")
        end,
        to = "weapon_overstated",
      },

      {
        "La conclusión defendible es trauma contundente perimortem.",
        when = function()
          return can_state_final_hypothesis()
        end,
        -- `run` corre al elegir la opción (no al entrar al nodo): acá se marca el
        -- caso como sostenido.
        run = function()
          set_flag("case.blunt_trauma_supported")
          set_flag("case.report_ready")
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
  -- Trampas: afirmaciones que la evidencia todavía no autoriza
  -- ---------------------------------------------------------------------------

  murder_overstated = {
    npc = {
      "No.",
      "Eso es una conclusión demasiado cómoda para la cantidad de evidencia que tiene.",
      "Puede hablar de trauma contundente perimortem.",
      "No puede hablar de asesinato sin intencionalidad, escena, agente y un poco menos de entusiasmo narrativo.",
    },
    to = "hub",
  },

  weapon_overstated = {
    npc = {
      "Todavía no.",
      "Puede mencionarlo como objeto lítico pesado recuperado en el sector.",
      "Para llamarlo arma necesitaríamos algo más: correspondencia de forma, residuos, marcas compatibles o una razón mejor que el peso.",
      "Por ahora es un objeto. No le atribuya carrera criminal.",
    },
    to = "hub",
  },

  -- ---------------------------------------------------------------------------
  -- Falta evidencia / pistas adaptativas
  -- ---------------------------------------------------------------------------

  not_ready = {
    npc = {
      "Eso es una respuesta aceptable, aunque poco heroica.",
      "Revise el cráneo, el tablero, la ficha osteológica y el registro contextual.",
      "Después vuelva con observaciones, no con corazonadas.",
    },
    to = END,
  },

  hint_after_attempts = {
    npc = {
      "Serrategui, está probando respuestas.",
      "Ordene el razonamiento: lesión, mecanismo, contexto, descarte.",
      "Si una hipótesis no explica esas cuatro cosas, todavía no es una hipótesis. Es una ocurrencia con vocabulario técnico.",
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
      "La hipótesis defendible es trauma contundente perimortem.",
      "Eso no dice quién lo hizo. Ni siquiera dice con certeza que haya sido intencional.",
      "Pero sí permite sostener una lectura prudente del caso.",
      "En arqueología, Serrategui, la prudencia no es timidez. Es higiene intelectual.",
    },
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
          return uttered("heavy_object")
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
      "Por ahora, que el objeto sea pesado no lo convierte en culpable.",
    },
    to = "after_solution",
  },
}
