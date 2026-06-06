-- Lab room behaviour. Static layout lives in lab.yaml.
--
-- Configurations (#185) are declared in lab.yaml under `configs:` (which actors /
-- objects are present), keyed by a symbolic id. The engine reconciles presence on
-- entry and tracks the live config; here we only supply the optional per-config
-- BEATS and the cross-config TRANSITIONS:
--
--   intro   Julia alone; the opening monologue (its on_first_enter, below).
--   puzzle  Julia + Dr. Schneider; the skull-trauma puzzle is active.
--   alone   Schneider has left; Julia alone again.
--
-- `set_room_config("lab", <id>)` switches config (reconciles presence + records
-- it); `current_room_config("lab")` reads it. No "lab.cfg" integer, no flow helper.

local room = {}

-- Per-config beats live here, keyed by the same ids as lab.yaml's `configs:`.
-- Only configs that have a beat need an entry; presence-only configs (puzzle,
-- alone) are fully described by YAML.
room.configs = {
    intro = include("rooms/lab/act1/intro.lua"), -- { on_first_enter = <monologue> }
}

local function schneider_present()
    return current_room_config("lab") == "puzzle"
end

local function puzzle_enabled()
    return current_room_config("lab") == "puzzle"
end

--------------------------------------------------------------------------------
-- Story beats / transitions. Wrapped in cutscene { ... } (#184) so the body
-- reads like a screenplay: no block_input/unblock_input ritual, flat
-- say/move/face verbs instead of avatar(id):method chains. Calling the beat
-- spawns it; the room view stays BLOCKED until the beat drains. Restoration is
-- C++-side, so a change_room mid-beat clears it cleanly even though scope
-- cancellation skips Lua cleanup (design 05 §Coroutine rules).
--------------------------------------------------------------------------------

-- "¡CLICK!" floating over the bluetooth speaker whenever it is toggled on/off.
-- Global so the intro config's beat (rooms/lab/act1/intro.lua) can reuse it.
function speaker_click()
    float_text("¡CLICK!", "bluetooth_speaker", { color = { r = 255, g = 230, b = 120 }, duration = 1.2 })
end

-- Dr. Schneider walks in from the door and stays for the puzzle. Handed back by
-- the llamas close-up via on_room_resume(schneider_arrives) (#186), so it plays
-- once the lab is the live, ticking room again. The beat does the walk-in
-- choreography, then `set_room_config("lab", "puzzle")` records the new config
-- (Schneider is already present, so the reconcile leaves him in place; on a later
-- reload the `puzzle` config re-spawns him at schneider_start with no walk-in).
schneider_arrives = cutscene(function()
    spawn_npc("schneider", "at_door", "down")
    move("schneider", "schneider_start")
    face("schneider", "left")

    say("schneider", "Serrategui.")
    say("player", "Dra. Schneider.")

    say("schneider", "¿Eso es música?")
    move("schneider", "at_desk")
    face("schneider", "up")
    say("schneider", "Interesante. Había asumido que era una falla eléctrica.")

    stop_music()
    speaker_click() -- apaga el parlante

    move("schneider", "schneider_start")
    face("schneider", "left")

    say("schneider", "Necesito sus observaciones sobre los restos de La Matilde antes del mediodía.")
    say("player", "Sí. Estaba empezando.")
    say("schneider", "Eso espero.")
    say("schneider", "La ficha preliminar ya existe. No necesito que la lea en voz alta.")
    say("schneider", "Necesito saber si puede defenderla.")
    say("schneider", "Una hipótesis no mejora por estar dicha con entusiasmo.")
    say("schneider", "Mejor si viene acompañada de evidencia.")

    set_room_config("lab", "puzzle")
end)

-- Schneider leaves again: switch to the `alone` config (the reconcile despawns
-- her, since `alone` lists no NPCs).
function schneider_leaves()
    set_room_config("lab", "alone")
end

--------------------------------------------------------------------------------
-- Pre-puzzle transition helpers (shared by the hotspots below).
--------------------------------------------------------------------------------

local function mark_context_glanced()
    facts.act1.context_glanced = true
end

local function thermo_can_be_used()
    return facts.act1.bones_glanced and facts.act1.context_glanced
end

local function use_thermo()
    if not facts.act1.bones_glanced then
        talk("player", "Primero los restos. Después el mate. Intentemos sostener una civilización mínima.")
        return
    end

    if not facts.act1.context_glanced then
        talk("player", "Podría tomar mate, sí.")
        talk("player", "Pero antes debería mirar aunque sea una evidencia que no tenga bombilla.")
        return
    end

    avatar("player"):move_to("intro_window")
    avatar("player"):face("left")
    talk("player", "Ahora sí. Un mate y después escribo algo razonable.")
    talk("player", "O algo razonablemente defendible, que en este instituto parece ser más importante.")
    talk("player", "No voy a mirar por la ventana.")
    wait(0.7)
    talk("player", "Sólo voy a vaciar el mate.")
    wait(0.7)
    talk("player", "...")
    talk("player", "Bueno. Una clasificación rápida.")

    facts.act1.thermo_checked = true
    facts.act1.window_distraction_enabled = true
end

local function maybe_open_window()
    if not facts.act1.window_distraction_enabled then
        talk("player", "La ventana está peligrosamente cerca de convertirse en una excusa.")
        if not facts.act1.bones_glanced then
            talk("player", "Primero los restos.")
        elseif not facts.act1.context_glanced then
            talk("player", "Y después el contexto. Schneider no aprobaría una fuga sin fundamentación.")
        else
            talk("player", "Quizá después de revisar el termo.")
        end
        return
    end

    open_closeup("window_llamas")
end

--------------------------------------------------------------------------------
room.hotspots = {
    window = {
        look_at = function()
            maybe_open_window()
        end,
        use = function()
            maybe_open_window()
        end,
    },

    -- Optional hotspot: add it to lab.yaml if you want a dedicated "termo/mate"
    -- target. If you do not add it, the flow still works through window.look_at
    -- after you call use_thermo() from whichever existing hotspot you prefer.
    stanley_clone_therm = {
        look_at = function()
            if facts.act1.thermo_checked then
                return "El termo ya cumplió su función: acercarme peligrosamente a la ventana."
            end
            return "El termo y el mate. Tecnología de concentración de baja complejidad y resultados variables."
        end,
        -- Verb handlers son auto-spawned (#183): los blocking primitives
        -- (talk/move_to/wait) andan sin envoltorio spawn(). Asignamos la función
        -- al verbo directamente.
        use = use_thermo,
    },

    skull_bones = {
        look_at = function()
            if not puzzle_enabled() then
                talk("player", "Bien. La Matilde. Entierro 1.")
                talk("player", "Cráneo con lesión visible, fragmentos asociados y una cantidad de silencio bastante poco colaborativa.")
                talk("player", "Antes de escribir cualquier cosa, necesito ordenar qué estoy mirando.")
                facts.act1.bones_glanced = true
            else
                open_closeup("lab_skull_closeup")
            end
        end,
        use = function()
            if not puzzle_enabled() then
                talk("player", "No. Manipular evidencia antes de revisar el contexto es una forma bastante rápida de dejar de ser becaria.")
            else
                talk("player", "Mejor observar antes de tocar. Sobre todo si Schneider está mirando.")
            end
        end,
    },

    chalkboard = {
        look_at = function()
            if not puzzle_enabled() then
                talk("player", "La Matilde, Unidad 7B, Capa 3.")
                talk("player", "El tablero tiene el contexto. Schneider diría que sin contexto un hueso es apenas una anécdota con calcio.")
                mark_context_glanced()
            else
                open_closeup("lab_chalkboard")
            end
        end,
        use = function()
            if not puzzle_enabled() then
                talk("player", "Puedo usar el tablero como mapa del problema, no como reemplazo de pensarlo.")
                mark_context_glanced()
            else
                open_closeup("lab_chalkboard")
            end
        end,
    },

    notebook1 = {
        look_at = function()
            if not puzzle_enabled() then
                talk("player", "La ficha osteológica preliminar ya sugiere trauma contundente perimortem.")
                talk("player", "Lo difícil no es repetir eso. Lo difícil es entender por qué se sostiene.")
                mark_context_glanced()
            else
                open_closeup("lab_notebook1")
            end
        end,
        use = function()
            if not puzzle_enabled() then
                talk("player", "Puedo usarla como punto de partida, no como sustituto de una idea.")
                mark_context_glanced()
            else
                open_closeup("lab_notebook1")
            end
        end,
    },

    notebook2 = {
        look_at = function()
            if not puzzle_enabled() then
                talk("player", "El registro contextual del hallazgo.")
                talk("player", "Porque un hueso sin contexto es casi una opinión.")
                mark_context_glanced()
            else
                open_closeup("lab_notebook2")
            end
        end,
        use = function()
            if not puzzle_enabled() then
                talk("player", "Esto debería ayudar a distinguir un golpe de una historia inventada con buena puntuación.")
                mark_context_glanced()
            else
                open_closeup("lab_notebook2")
            end
        end,
    },

    -- The NPC-bound hotspot (see lab.yaml `bind: npc:schneider`). Talking to her
    -- opens the skull-trauma puzzle dialog; she speaks it (start_dialog's 2nd arg).
    schneider = {
        look_at = function()
            if not schneider_present() then
                return "La Dra. Schneider todavía no está en el laboratorio. Lo cual, por ahora, mantiene estable mi presión arterial."
            end
            return "La Dra. Schneider. Espera mis observaciones sobre los restos."
        end,
        talk_to = function()
            if not schneider_present() then
                talk("player", "Todavía no. Primero debería producir algo que pueda sobrevivir a una pregunta de Schneider.")
            else
                start_dialog("skull_trauma_cause", "schneider")
            end
        end,
    },

    door = {
        look_at = function()
            return "La puerta da al pasillo del instituto."
        end,
        use = function()
            -- play_sound("sfx/door_open.ogg")
            change_room("hall", "from_lab")
        end,
    },

    computer = {
        look_at = function()
            return "La computadora de escritorio. No tiene acceso a internet, pero al menos no es un dinosaurio."
        end,

        use = function()
            if not facts.case.schneider_dialog_done then
                return "Primero tengo que poder defender la hipótesis."
            end

            -- Preparar el informe en la compu cierra el Acto 1
            start_cutscene("act1_outro")
        end,
    },
}

return room
