-- Lab room behavior. Static layout lives in lab.yaml.
local room = {}

function intro()
    block_input()

    avatar("player"):face("left")
    wait(2)
    avatar("player"):face("right")
    wait(2)
    
    avatar("player"):move_to("player_start")
    avatar("player"):face("down")
    wait(2)
    
    talk("player", "¡Hola!")
    
    talk("player", "Soy Julia, la nueva becaria del instituto")

    talk("player", "Estoy muy emocionada por empezar a trabajar aquí, aunque también un poco nerviosa")

    talk("player", "Estaré a cargo de La Dra. Schneider.")

    talk("player", "Que dicen que es una persona muy estricta y exigente")

    talk("player", "Así que debería dejar de mirar por la ventana y ponerme a trabajar.")

    talk("player", "Lo que pasa es que miro por la ventana porque a veces se ven llamas, alpacas y otros animales que no acostumbran a verse en la ciudad.")

    talk("player", "Y también miro por la ventana porque tengo cierta facilidad para distraerme de mis labores.")

    talk("player", "A las cuales debería retornar")

    talk("player", "O mejor dicho, empezar")

    talk("player", "Porque no hice nada productivo desde que ingresé al instituto")

    avatar("player"):move_to("at_desk")

    avatar("player"):face("up")
   
    play_music("music/seretuarqueologo.mp3")

    wait(2)

    avatar("player"):face("down")

    wait(1)
 
    avatar("player"):move_to("player_start")

    avatar("player"):face("down")

    talk("player", "Por cierto, este juego tiene una banda sonora asombrosa.")

    wait(1)

    talk("player", "Y esto es sólo el principio.")

    set_state("lab.julia_intro_seen", true)
    unblock_input()
end

function dr_schneider_enters_the_room()
  -- Schneider is NOT a permanent resident of the lab: she only exists for this
  -- scene. spawn_npc() creates her from the cast entry "schneider" and seats her
  -- at a named room point (here the door), facing into the room.
  block_input()
  spawn_npc("schneider", "at_door", "down")

  -- She is now a normal room NPC: avatar("schneider") drives her like any avatar.
  avatar("schneider"):move_to("at_desk") -- walks in (uses room pathfinding)
  avatar("schneider"):face("down")
  talk("schneider", "¿Usted es la nueva becaria? Llega tarde.")
  talk("player",    "Eh... sí. Perdón, Dra. Schneider.")
  talk("schneider", "Clasifique esos huesos antes del mediodía. No me decepcione.")

  -- Send her back to the door, then remove her from the room. After despawn_npc
  -- she is gone; nothing re-creates her because on_load never spawns her.
  avatar("schneider"):move_to("at_door")
  despawn_npc("schneider")
  unblock_input()
end

-- Make Dr. Schneider enter the lab and STAY (so the player can talk to her for the
-- skull puzzle). NPC presence is not persisted, so this runs from on_load each time;
-- spawn_npc is idempotent (it repositions her if she already exists). Blocking calls
-- (move_to) must run inside a spawned task.
function schneider_enters()
  spawn(function()
    spawn_npc("schneider", "at_door", "down")
    avatar("schneider"):move_to("schneider_start")
    avatar("schneider"):face("left")
  end)
end

function room.on_load()
  -- Intro cutscene commented out for now so we can focus on the Schneider puzzle.
  -- if get_state("lab.julia_intro_seen") then return end
  -- spawn(function() intro(); dr_schneider_enters_the_room() end)

  -- TEMP: seed the puzzle "findings" so the dialog's observation options appear
  -- before the close-up evidence is wired. Comment this block once the close-ups
  -- set these via set_state("finding.<x>", true).
  set_state("finding.radial_fractures", true)
  set_state("finding.no_cut_marks", true)
  set_state("finding.no_collapse", true)
  set_state("finding.primary_burial", true)
  set_state("finding.heavy_lithic_object", true)
  set_state("finding.perimortem_possible", true)

  schneider_enters()
end
function room.on_unload() end

-- room.on_zone_enter = function(zone)
--   if zone == "to_hall" then
--     change_room("hall", "from_study")
--   end
-- end

room.hotspots = {  
  window = {
    look_at = function()
      open_closeup("window_llamas")
      return "La ventana en la que a veces aparecen llamas, otras alpacas, y otras ambas."
    end,
  },
  skull_bones = {
    look_at = function()
      open_closeup("lab_skull_closeup")
      return "Los huesos que tengo que clasificar."
    end,
  },
  chalkboard = {
    look_at = function()
      open_closeup("lab_chalkboard")
      return "El pizarrón del laboratorio."
    end,
  },
  notebook1 = {
    look_at = function()
      open_closeup("lab_notebook1")
      return "Un cuaderno de notas."
    end,
  },
  notebook2 = {
    look_at = function()
      open_closeup("lab_notebook2")
      return "Otro cuaderno de notas."
    end,
  },

  -- The NPC-bound hotspot (see lab.yaml `bind: npc:schneider`). Talking to her opens
  -- the skull-trauma puzzle dialog; she speaks it (start_dialog's 2nd arg).
  schneider = {
    look_at = function()
      return "La Dra. Schneider. Espera mis observaciones sobre los restos."
    end,
    talk_to = function()
      start_dialog("skull_trauma_cause", "schneider")
    end,
  },

  door = {
    look_at = function()
      return "La puerta da al pasillo del instituto."
    end,
    open = function()
      --play_sound("sfx/door_open.ogg")
      change_room("hall", "from_lab")  
      return ""    
    end,
  }
}

return room
