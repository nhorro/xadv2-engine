-- Window close-up (window_llamas): Julia gets distracted from her bone-sample
-- report by the "unidentified animals" outside — llamas and alpacas. Purposes:
--   * Character: Julia is curious, warm, a little divergent.
--   * Trigger: after she identifies 3, Dr. Schneider starts nagging off-screen
--     (shout banner); backing out switches the lab to configuration 2 (Schneider
--     present) — see rooms/lab.lua.
--
-- DEPENDS ON engine PR #164 (set_hotspot_name + shout). Until that is merged and
-- ingreso-urgente is synced with develop, those two calls are guarded below, so
-- the scene still runs (Julia talks; renames/shouts simply no-op). Once synced,
-- you can drop the `rename`/`voice` guards and call set_hotspot_name/shout direct.
--
-- TEXT IS DRAFT — rewrite the descriptions / labels / shout lines to taste.

local CFG_PUZZLE = 2 -- keep in sync with rooms/lab.lua (CFG_PUZZLE)
local NEEDED = 3     -- identifications before Schneider starts nagging

-- id -> what Julia says when she examines it + the label it becomes once named.
local animals = {
  camelid1 = {
    evidence = "camelid1_profile",
    label = "Camélido 1 — posible llama",
    lines = {
      "Cuello largo, orejas curvas, expresión de superioridad administrativa.",
      "Llama. O al menos, lo bastante llama como para arriesgar una clasificación desde una ventana.",
    },
  },
  camelid2 = {
    evidence = "camelid2_profile",
    label = "Camélido 2 — posible alpaca",
    lines = {
      "Más compacta, más lanuda, menos interesada en dominar el paisaje.",
      "Alpaca. Tiene esa forma de nube con patas que finge no estar siendo observada.",
    },
  },
  camelid3 = {
    evidence = "camelid3_profile",
    label = "Camélido 3 — alpaca probable",
    lines = {
      "Otra alpaca, creo. Cara redonda, lana abundante, actitud de haber entendido el trámite antes que yo.",
      "La evidencia no es concluyente, pero mi hipótesis de alpaca es defendible.",
    },
  },
  camelid4 = {
    evidence = "camelid4_profile",
    label = "Camélido 4 — llama probable",
    lines = {
      "Esa sí tiene arquitectura de llama: alta, angulosa y con orejas de signo de pregunta.",
      "Una llama. Parece estar evaluando el instituto desde afuera, lo cual es prudente.",
    },
  },
  camelid5 = {
    evidence = "camelid5_profile",
    label = "Camélido 5 — posible alpaca",
    lines = {
      "Compacta, lanuda, algo ofendida por existir bajo categorías humanas.",
      "Alpaca, probablemente. Aunque podría ser una oveja con aspiraciones andinas.",
    },
  },
  camelid6 = {
    evidence = "camelid6_profile",
    label = "Camélido 6 — clasificación dudosa",
    lines = {
      "Ese está en sombra. Perfil oscuro, datos insuficientes, dignidad intacta.",
      "Camélido sospechoso. Julia Serrategui se reserva el derecho de corregir esta identificación en una versión futura del informe.",
    },
  },
}

-- Schneider's off-screen interruption. Keep it dry, not cartoonishly angry.
local NAG = {
  "Serrategui.",
  "Si ya terminó de clasificar camélidos por la ventana, necesito sus observaciones sobre La Matilde.",
  "Los restos de la mesa tienen prioridad metodológica sobre los rumiantes del paisaje.",
}

-- --- guards for the in-flight engine primitives (drop once #164 is synced) ---
local function rename(id, label)
  if set_hotspot_name then set_hotspot_name(id, label) end
end
local function voice(text)
  if shout then shout(text) end
end

local function count_identified()
  local n = 0
  for id in pairs(animals) do
    if get_state("llamas." .. id .. ".done") then
      n = n + 1
    end
  end
  return n
end

-- Off-screen nagging loop: runs in the close-up scope, so it is cancelled (and
-- the banner clears) when the player backs out. Started once we hit NEEDED.
local shouting = false
local function start_shouting()
  if shouting then
    return
  end
  shouting = true
  spawn(function()
    local i = 1
    while true do
      voice(NAG[i])
      i = i % #NAG + 1
      wait(3.5)
    end
  end)
end

local function identify(id)
  local a = animals[id]
  for _, line in ipairs(a.lines) do
    talk("player", line)
  end
  rename(id, a.label)
  set_state("llamas." .. id .. ".done", true)
  set_state("llamas." .. id .. ".label", a.label) -- persist the chosen label
  discover_evidence(a.evidence)
  if count_identified() >= NEEDED then
    start_shouting()
  end
end

-- Build a click handler per animal from the table above.
local function build_hotspots()
  local t = {}
  for id in pairs(animals) do
    t[id] = function() identify(id) end
  end
  return t
end

return {
  on_enter = function()
    -- Re-apply identifications from a previous visit, and resume the nagging if we
    -- already crossed the threshold (state persists; the running task did not).
    for id in pairs(animals) do
      local label = get_state("llamas." .. id .. ".label")
      if label then
        rename(id, label)
      end
    end
    if count_identified() >= NEEDED then
      start_shouting()
    end
  end,

  on_exit = function()
    -- Once Schneider is nagging, leaving the window means Julia turns around and
    -- there she is: switch the lab to configuration 2 (Schneider present). The room
    -- is frozen beneath the overlay, so this instant change is seen on back-out.
    -- (Assumes the lab started without Schneider — i.e. lab.cfg was CFG_INTRO.)
    if count_identified() >= NEEDED then
      set_state("lab.cfg", CFG_PUZZLE)
      -- A close-up can't run the beat's blocking moves/talk (the room is frozen
      -- beneath the overlay), so hand it back: on_room_resume(...) plays the lab's
      -- schneider_arrives() the moment the live room is ticking again (#186).
      on_room_resume(schneider_arrives)
    end
  end,

  hotspots = build_hotspots(),
}
