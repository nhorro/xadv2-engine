-- Lab room behaviour. Static layout lives in lab.yaml.
--
-- ROOM CONFIGURATIONS (shared convention documented in scripts/game.lua)
-- The lab is shown in one of three configurations, read from the global state
-- key "lab.cfg" and (re)built in on_load by configure():
--
--   CFG_INTRO   Julia alone; the opening monologue.
--   CFG_PUZZLE  Julia + Dr. Schneider; the skull-trauma puzzle is active
--               (4 close-ups feed findings, then her dialog tree).
--   CFG_ALONE   Schneider has left; Julia alone again.
--
-- configure(c) uses only INSTANT calls (spawn/despawn, show/hide) so it is safe
-- to run every load. The STORY BEATS below (intro, arrival, exit) are blocking
-- and must run inside spawn(); each ends by advancing "lab.cfg".

local room = {}

local CFG_INTRO = 1
local CFG_PUZZLE = 2
local CFG_ALONE = 3

local function cfg()
    -- Story default: Julia starts alone. If you need to dogfood the puzzle,
    -- temporarily replace CFG_INTRO with CFG_PUZZLE.
    return get_state("lab.cfg") or CFG_INTRO
end

local function schneider_present()
    return get_state("act1.schneider_present") or cfg() == CFG_PUZZLE
end

local function puzzle_enabled()
    return get_state("act1.puzzle_enabled") or cfg() == CFG_PUZZLE
end

--------------------------------------------------------------------------------
-- Configuration setup (INSTANT calls only — no wait/talk/move_to here).
-- Exhaustive + idempotent: spawn what belongs, despawn the rest.
--------------------------------------------------------------------------------
local function configure(c)
    if c == CFG_PUZZLE then
        -- Schneider is present for the puzzle. The walk-in animation is the
        -- schneider_arrives() beat; this is only for restoring an already-set state.
        spawn_npc("schneider", "schneider_start", "left")
        set_state("act1.schneider_present", true)
        set_state("act1.puzzle_enabled", true)

        -- TEMP: seed the puzzle "findings" so the dialog's observation options
        -- appear before the close-ups set them. Remove this once each close-up
        -- handler calls set_state("finding.<x>", true).
        set_state("finding.radial_fractures", true)
        set_state("finding.no_cut_marks", true)
        set_state("finding.no_collapse", true)
        set_state("finding.primary_burial", true)
        set_state("finding.heavy_lithic_object", true)
        set_state("finding.perimortem_possible", true)
    else
        -- CFG_INTRO and CFG_ALONE: nobody in the lab but Julia (the player).
        despawn_npc("schneider")
        if c == CFG_INTRO then
            set_state("act1.schneider_present", false)
            set_state("act1.puzzle_enabled", false)
        end
    end
end

--------------------------------------------------------------------------------
-- Story beats / transitions (BLOCKING — always call inside spawn()).
--------------------------------------------------------------------------------

-- "¡CLICK!" floating over the bluetooth speaker whenever it is toggled on/off.
-- Guarded so the scene still runs before engine PR #165 (float_text) is merged +
-- synced; once it is, drop the `if float_text` guard.
local function speaker_click()
    if float_text then
        float_text("¡CLICK!", "bluetooth_speaker", { color = { r = 255, g = 230, b = 120 }, duration = 1.2 })
    end
end

-- CFG_INTRO: Julia introduces herself. The player then gets control and must
-- begin examining the La Matilde materials before the window distraction is enabled.
function intro()
    block_input()

    avatar("player"):face("left")
    wait(2)
    avatar("player"):face("right")
    wait(2)

    avatar("player"):move_to("player_start")
    avatar("player"):face("down")
    wait(2)

    talk("player", "Ah. Hola.")
    talk("player", "No esperaba que ya hubiera alguien mirando.")
    talk("player", "Soy Julia. Becaria nueva del Instituto Arqueológico Serra Ventana.")
    talk("player", "Hoy empiezo con tareas de clasificación bajo supervisión de la Dra. Schneider.")
    talk("player", "Me dijeron que es estricta.")
    talk("player", "Después me aclararon que 'estricta' era una forma amable de decirlo.")
    talk("player", "Así que debería estar trabajando.")

    talk("player",
        "Pero por esa ventana a veces se ven llamas. Y alpacas. Y otros animales que en la ciudad no suelen circular con tanta libertad institucional.")

    talk("player",
        "Además tengo una facilidad bastante desarrollada para distraerme de mis obligaciones.")

    talk("player", "A las que debería volver.")
    talk("player", "O empezar.")
    talk("player", "Porque, técnicamente, todavía no produje nada defendible ante una comisión evaluadora.")

    avatar("player"):move_to("at_desk")
    avatar("player"):face("up")
    talk("player", "Para clasificar restos arqueológicos necesito música.")
    talk("player", "No sé si es metodológicamente válido, pero mejora mi predisposición laboral.")

    speaker_click() -- enciende el parlante
    play_music("music/seretuarqueologo.mp3")
    wait(2)

    avatar("player"):face("down")
    wait(1)

    avatar("player"):move_to("at_skull_bones")
    avatar("player"):face("up")
    talk("player", "Bien. La Matilde. Unidad 7B. Capa 3. Entierro 1.")
    talk("player", "Tengo que preparar un informe preliminar antes de que la Dra. Schneider descubra que todavía no empecé.")

    set_state("lab.julia_intro_seen", true)
    unblock_input()
end

-- CFG_PUZZLE: Dr. Schneider walks in from the door and stays for the puzzle.
-- Triggered after the llamas close-up asks for "lab.schneider_cue".
function schneider_arrives()
    block_input()

    set_state("lab.cfg", CFG_PUZZLE)
    set_state("act1.schneider_present", true)
    set_state("act1.puzzle_enabled", true)

    spawn_npc("schneider", "at_door", "down")
    avatar("schneider"):move_to("schneider_start")
    avatar("schneider"):face("left")

    talk("schneider", "Serrategui.")
    talk("player", "Dra. Schneider.")

    talk("schneider", "¿Eso es música?")
    avatar("schneider"):move_to("at_desk")
    avatar("schneider"):face("up")
    talk("schneider", "Interesante. Había asumido que era una falla eléctrica.")

    stop_music()
    speaker_click() -- apaga el parlante

    avatar("schneider"):move_to("schneider_start")
    avatar("schneider"):face("left")

    talk("schneider", "Necesito sus observaciones sobre los restos de La Matilde antes del mediodía.")
    talk("player", "Sí. Estaba empezando.")
    talk("schneider", "Eso espero.")
    talk("schneider", "La ficha preliminar ya existe. No necesito que la lea en voz alta.")
    talk("schneider", "Necesito saber si puede defenderla.")
    talk("schneider", "Una hipótesis no mejora por estar dicha con entusiasmo.")
    talk("schneider", "Mejor si viene acompañada de evidencia.")

    unblock_input()
end

-- CFG_ALONE: a short text "cutscene" sends Schneider away again.
function schneider_leaves()
    block_input()
    despawn_npc("schneider")
    set_state("lab.cfg", CFG_ALONE)
    set_state("act1.schneider_present", false)
    set_state("act1.puzzle_enabled", false)
    unblock_input()
end

-- The llamas close-up can't run blocking room choreography (the room is frozen
-- under the overlay), so it leaves a cue in state. Once we are back in the live,
-- ticking room this watcher plays Schneider's arrival beat. Armed from on_load.
local function arm_schneider_cue()
    spawn(function()
        while not get_state("lab.schneider_cue") do
            wait(0.1)
        end
        set_state("lab.schneider_cue", false)
        wait(0.1) -- let the close-up finish closing before the beat starts
        schneider_arrives()
    end)
end

--------------------------------------------------------------------------------
function room.on_load()
    configure(cfg())

    if cfg() == CFG_INTRO and not get_state("lab.julia_intro_seen") then
        spawn(function() intro() end)
    end

    -- Arm while Julia is alone, or when the cue was persisted before reload.
    if cfg() == CFG_INTRO or get_state("lab.schneider_cue") then
        arm_schneider_cue()
    end
end

function room.on_unload() end

-- room.on_zone_enter = function(zone)
--   if zone == "to_hall" then
--     change_room("hall", "from_study")
--   end
-- end

--------------------------------------------------------------------------------
-- Pre-puzzle transition helpers
--------------------------------------------------------------------------------

local function mark_context_glanced()
    set_state("act1.context_glanced", true)
end

local function thermo_can_be_used()
    return get_state("act1.bones_glanced") and get_state("act1.context_glanced")
end

local function use_thermo()
    if not get_state("act1.bones_glanced") then
        talk("player", "Primero los restos. Después el mate. Intentemos sostener una civilización mínima.")
        return
    end

    if not get_state("act1.context_glanced") then
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

    set_state("act1.thermo_checked", true)
    set_state("act1.window_distraction_enabled", true)
end

local function maybe_open_window()
    if not get_state("act1.window_distraction_enabled") then
        talk("player", "La ventana está peligrosamente cerca de convertirse en una excusa.")
        if not get_state("act1.bones_glanced") then
            talk("player", "Primero los restos.")
        elseif not get_state("act1.context_glanced") then
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
            return ""
        end,
        use = function()
            maybe_open_window()
            return ""
        end,
    },

    -- Optional hotspot: add it to lab.yaml if you want a dedicated "termo/mate"
    -- target. If you do not add it, the flow still works through window.look_at
    -- after you call use_thermo() from whichever existing hotspot you prefer.
    thermo = {
        look_at = function()
            if get_state("act1.thermo_checked") then
                return "El termo ya cumplió su función: acercarme peligrosamente a la ventana."
            end
            return "El termo y el mate. Tecnología de concentración de baja complejidad y resultados variables."
        end,
        use = function()
            use_thermo()
            return ""
        end,
    },

    skull_bones = {
        look_at = function()
            if not puzzle_enabled() then
                talk("player", "Bien. La Matilde. Entierro 1.")
                talk("player", "Cráneo con lesión visible, fragmentos asociados y una cantidad de silencio bastante poco colaborativa.")
                talk("player", "Antes de escribir cualquier cosa, necesito ordenar qué estoy mirando.")
                set_state("act1.bones_glanced", true)
            else
                open_closeup("lab_skull_closeup")
            end
            return ""
        end,
        use = function()
            if not puzzle_enabled() then
                talk("player", "No. Manipular evidencia antes de revisar el contexto es una forma bastante rápida de dejar de ser becaria.")
            else
                talk("player", "Mejor observar antes de tocar. Sobre todo si Schneider está mirando.")
            end
            return ""
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
            return ""
        end,
        use = function()
            if not puzzle_enabled() then
                talk("player", "Puedo usar el tablero como mapa del problema, no como reemplazo de pensarlo.")
                mark_context_glanced()
            else
                open_closeup("lab_chalkboard")
            end
            return ""
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
            return ""
        end,
        use = function()
            if not puzzle_enabled() then
                talk("player", "Puedo usarla como punto de partida, no como sustituto de una idea.")
                mark_context_glanced()
            else
                open_closeup("lab_notebook1")
            end
            return ""
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
            return ""
        end,
        use = function()
            if not puzzle_enabled() then
                talk("player", "Esto debería ayudar a distinguir un golpe de una historia inventada con buena puntuación.")
                mark_context_glanced()
            else
                open_closeup("lab_notebook2")
            end
            return ""
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
            return ""
        end,
    },

    door = {
        look_at = function()
            return "La puerta da al pasillo del instituto."
        end,
        open = function()
            -- play_sound("sfx/door_open.ogg")
            change_room("hall", "from_lab")
            return ""
        end,
    },
}

return room
