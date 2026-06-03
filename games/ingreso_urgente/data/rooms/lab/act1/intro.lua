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
-- beat). Runs inside spawn() via the flow helper.
function M.on_first_enter()
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

return M
