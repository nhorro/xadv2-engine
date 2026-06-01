-- Window close-up (window_llamas): Julia gets distracted from her bone-sample
-- report by the "unidentified camélidos" outside — llamas and alpacas. Purposes:
--   * Character: Julia is curious, warm, a little divergent.
--   * Trigger: after she identifies 3, Dr. Schneider starts nagging off-screen
--     (shout banner); backing out raises "lab.schneider_cue", and lab.lua plays
--     the actual arrival beat (including walking in and turning off the music).
--
-- DEPENDS ON engine PR #164 (set_hotspot_name + shout). Until that is merged and
-- ingreso-urgente is synced with develop, those two calls are guarded below, so
-- the scene still runs (Julia talks; renames/shouts simply no-op). Once synced,
-- you can drop the `rename`/`voice` guards and call set_hotspot_name/shout direct.

local NEEDED = 3 -- identifications before Schneider starts nagging

-- Keep these keys in sync with the close-up YAML hotspot ids.
-- If your YAML ids were renamed to camelid1..camelid6, rename these keys too.
local animals = {
  animal1 = {
    label = "Camélido 1 — posible llama",
    lines = {
      "Cuello largo, orejas curvas, expresión de superioridad administrativa.",
      "Llama. O al menos, lo bastante llama como para arriesgar una clasificación desde una ventana.",
    },
  },
  animal2 = {
    label = "Camélido 2 — posible alpaca",
    lines = {
      "Más compacta, más lanuda, menos interesada en dominar el paisaje.",
      "Alpaca. Tiene esa forma de nube con patas que finge no estar siendo observada.",
    },
  },
  animal3 = {
    label = "Camélido 3 — alpaca probable",
    lines = {
      "Otra alpaca, creo. Cara redonda, lana abundante, actitud de haber entendido el trámite antes que yo.",
      "La evidencia no es concluyente, pero mi hipótesis de alpaca es defendible.",
    },
  },
  animal4 = {
    label = "Camélido 4 — llama probable",
    lines = {
      "Esa sí tiene arquitectura de llama: alta, angulosa y con orejas de signo de pregunta.",
      "Una llama. Parece estar evaluando el instituto desde afuera, lo cual es prudente.",
    },
  },
  animal5 = {
    label = "Camélido 5 — posible alpaca",
    lines = {
      "Compacta, lanuda, algo ofendida por existir bajo categorías humanas.",
      "Alpaca, probablemente. Aunque podría ser una oveja con aspiraciones andinas.",
    },
  },
  animal6 = {
    label = "Camélido 6 — clasificación dudosa",
    lines = {
      "Ese está en sombra. Perfil oscuro, datos insuficientes, dignidad intacta.",
      "Camélido sospechoso. Julia Serrategui se reserva el derecho de corregir esta identificación en una versión futura del informe.",
    },
  },
}

-- Schneider's off-screen interruption. Dry, not cartoonishly angry.
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
  if not a then
    return
  end

  if get_state("llamas." .. id .. ".done") then
    talk("player", get_state("llamas." .. id .. ".label") or "Camélido ya clasificado.")
    return
  end

  for _, line in ipairs(a.lines) do
    talk("player", line)
  end

  rename(id, a.label)
  set_state("llamas." .. id .. ".done", true)
  set_state("llamas." .. id .. ".label", a.label) -- persist the chosen label

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
    -- there she is. The room script owns the blocking choreography, so we only set
    -- a cue here.
    if count_identified() >= NEEDED then
      set_state("lab.schneider_cue", true)
    end
  end,

  hotspots = build_hotspots(),
}
