-- Stan, el portero del instituto. Diálogo de muestra para M5a.
-- Cubre: NPC multilínea, opciones gated por `when`, `once`, y `run` que muta
-- inventario. El nodo `greet` ejerce de hub y END termina el diálogo.
return {
  start = "greet",

  greet = {
    npc = "Buenas noches, Julia. ¿Algo en lo que pueda ayudarte?",
    options = {
      { "¿Viste algo raro últimamente?", to = "raro" },
      {
        "Perdí mi cuaderno, ¿lo viste?",
        when = function() return not has_item("notebook") end,
        to = "cuaderno",
      },
      {
        "Toma, deja a buen recaudo el cuaderno.",
        when = function() return has_item("notebook") end,
        once = true,
        run = function() remove_item("notebook") end,
        to = "regalo",
      },
      { "Buenas noches.", to = END },
    },
  },

  raro = {
    npc = {
      "Sólo gente entrando y saliendo, lo de siempre.",
      "Aunque... el doctor anduvo aquí hasta muy tarde.",
    },
    to = "greet",
  },

  cuaderno = {
    npc = "No, no lo vi. Mira en tu estudio, querida.",
    to = "greet",
  },

  regalo = {
    npc = "Gracias, lo guardaré en el cajón hasta mañana.",
    to = END,
  },
}
