-- Stan, el portero del instituto. Diálogo de muestra para M5a.
--
-- Idiom canónico "intro → hub": el saludo `intro` habla una sola vez y salta
-- al `hub`, que es un nodo solo-opciones. Los nodos de rama vuelven `to =
-- "hub"` para no re-saludar. Si el hub tuviera su propio `npc`, se hablaría
-- en cada vuelta (consistente con §Dialog execution del diseño, pero molesto
-- en la práctica).
--
-- El estado `stan.received_notebook` (global, sobrevive cambios de cuarto y
-- save/load cuando exista) separa los tres mundos del cuaderno: nunca lo
-- tuviste, lo tienes, o ya se lo entregaste a Stan.
return {
  start = "intro",

  intro = {
    npc = "Buenas noches, Julia. ¿Algo en lo que pueda ayudarte?",
    to = "hub",
  },

  hub = {
    options = {
      { "¿Viste algo raro últimamente?", to = "raro" },
      {
        "Perdí mi cuaderno, ¿lo viste?",
        when = function()
          return not has_item("notebook") and not get_state("stan.received_notebook")
        end,
        to = "cuaderno_perdido",
      },
      {
        "Toma, deja a buen recaudo el cuaderno.",
        when = function() return has_item("notebook") end,
        once = true,
        run = function()
          remove_item("notebook")
          set_state("stan.received_notebook", true)
        end,
        to = "regalo",
      },
      {
        "¿Está a salvo el cuaderno?",
        when = function() return get_state("stan.received_notebook") == true end,
        to = "cuaderno_guardado",
      },
      { "Buenas noches.", to = END },
    },
  },

  raro = {
    npc = {
      "Sólo gente entrando y saliendo, lo de siempre.",
      "Aunque... el doctor anduvo aquí hasta muy tarde.",
    },
    to = "hub",
  },

  cuaderno_perdido = {
    npc = "No, no lo vi. Mira en tu estudio, querida.",
    to = "hub",
  },

  regalo = {
    npc = "Gracias, lo guardaré en el cajón hasta mañana.",
    to = "hub",
  },

  cuaderno_guardado = {
    npc = "Tranquila, lo tengo a buen recaudo en el cajón.",
    to = "hub",
  },
}
