-- lab — cfg 1: INTRO. Julia alone; the opening monologue (one-time).
local M = {}

-- INSTANT presence: nobody but Julia; puzzle off.
function M.configure(c)
    despawn_npc("schneider")
    set_state("act1.schneider_present", false)
    set_state("act1.puzzle_enabled", false)
end

-- First time the player enters the lab: Julia introduces herself, turns on the
-- music, and is told (by herself) to start classifying the La Matilde materials.
-- `speaker_click` is a global helper defined in lab.lua (shared with the arrival
-- beat). Wrapped in cutscene { ... } (#184): the body reads like a screenplay,
-- the room view stays BLOCKED until the beat drains, and the flow helper still
-- calls this through spawn() — the outer task just arms the inner cutscene and
-- dies immediately.
M.on_first_enter = cutscene(function()
    face("player", "left")
    wait(2)
    face("player", "right")
    wait(2)

    move("player", "player_start")
    face("player", "down")
    wait(2)

    say("player", "Ah. Hola.")
    say("player", "No esperaba que ya hubiera alguien mirando.")
    say("player", "Soy Julia. Becaria nueva del Instituto Arqueológico Serra Ventana.")
    say("player", "Hoy empiezo con tareas de clasificación bajo supervisión de la Dra. Schneider.")
    say("player", "Me dijeron que es estricta.")
    say("player", "Después me aclararon que 'estricta' era una forma amable de decirlo.")
    say("player", "Así que debería estar trabajando.")

    say("player",
        "Pero por esa ventana a veces se ven llamas. Y alpacas. Y otros animales que en la ciudad no suelen circular con tanta libertad institucional.")

    say("player",
        "Además tengo una facilidad bastante desarrollada para distraerme de mis obligaciones.")

    say("player", "A las que debería volver.")
    say("player", "O empezar.")
    say("player", "Porque, técnicamente, todavía no produje nada defendible ante una comisión evaluadora.")

    move("player", "at_desk")
    face("player", "up")
    say("player", "Para clasificar restos arqueológicos necesito música.")
    say("player", "No sé si es metodológicamente válido, pero mejora mi predisposición laboral.")

    speaker_click() -- enciende el parlante
    play_music("music/seretuarqueologo.mp3")
    wait(2)

    face("player", "down")
    wait(1)

    move("player", "at_skull_bones")
    face("player", "up")
    say("player", "Bien. La Matilde. Unidad 7B. Capa 3. Entierro 1.")
    say("player", "Tengo que preparar un informe preliminar antes de que la Dra. Schneider descubra que todavía no empecé.")

    set_state("lab.julia_intro_seen", true)
end)

return M
