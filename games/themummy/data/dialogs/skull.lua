-- calavera.lua
--
-- Diálogo de muestra con una calavera encontrada en el instituto.
-- Inspirado libremente en Shakespeare / Hamlet, pero adaptado a una
-- aventura gráfica con tono irónico.
--
-- Estados:
--   skull.was_named       : Julia ya bautizó a la calavera.
--   skull.told_secret     : la calavera ya dio su pista principal.
--   skull.received_flower : Julia ya le dejó una flor.
--
-- Requiere opcionalmente:
--   item: "dry_flower"

return {
  -- The skull is a fixed prop (not an avatar), so anchor its dialog lines at the
  -- shelf where it sits instead of the screen centre.
  text_anchor = "skull_talk_spot",
  start = "intro",

  intro = {
    npc = "La calavera parece mirarte con la paciencia de quien ya no tiene párpados.",
    to = "hub",
  },

  hub = {
    options = {
      { "¿Quién eres?", to = "quien" },

      {
        "¿Puedo llamarte Yorick?",
        when = function()
          return not get_state("skull.was_named")
        end,
        run = function()
          set_state("skull.was_named", true)
        end,
        to = "yorick",
      },

      {
        "¿Qué viste en este instituto?",
        when = function()
          return get_state("skull.was_named") == true
        end,
        to = "secreto",
      },

      {
        "Te traje una flor seca.",
        when = function()
          return has_item("dry_flower") and not get_state("skull.received_flower")
        end,
        run = function()
          remove_item("dry_flower")
          set_state("skull.received_flower", true)
        end,
        to = "flor",
      },

      {
        "¿Sigues ahí?",
        when = function()
          return get_state("skull.told_secret") == true
        end,
        to = "sigues",
      },

      { "Mejor dejo de hablar con restos humanos.", to = END },
    },
  },

  quien = {
    npc = {
      "Fui muchas cosas, según quién preguntara.",
      "Para mi madre, un niño prometedor.",
      "Para mis acreedores, una decepción con piernas.",
      "Para la ciencia, una muestra mal etiquetada.",
      "Ahora, al parecer, soy conversación."
    },
    to = "hub",
  },

  yorick = {
    npc = {
      "¿Yorick?",
      "Hubiera preferido un nombre menos usado por estudiantes melancólicos.",
      "Pero acepto. A esta altura, la vanidad es un lujo de los mandibulados."
    },
    to = "hub",
  },

  secreto = {
    npc = {
      "Vi al doctor entrar de noche, cuando el reloj fingía estar detenido.",
      "Llevaba una caja pequeña, envuelta en papel de archivo.",
      "No fue hacia los laboratorios.",
      "Fue hacia el depósito viejo, donde las cosas olvidadas aprenden a esperar."
    },
    run = function()
      set_state("skull.told_secret", true)
    end,
    to = "hub",
  },

  flor = {
    npc = {
      "Una flor seca.",
      "Qué gesto tan humano: ofrecerle muerte decorativa a la muerte estructural.",
      "Gracias, Julia.",
      "La pondré aquí, junto a mi absoluta falta de pulmones."
    },
    to = "hub",
  },

  sigues = {
    npc = {
      "Sigo aquí.",
      "Ser o no ser ya no es la cuestión.",
      "La cuestión es quién cerró el depósito desde adentro."
    },
    to = "hub",
  },
}