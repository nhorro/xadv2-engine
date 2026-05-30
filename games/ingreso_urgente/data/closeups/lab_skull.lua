-- Scripted behavior for the lab_skull close-up. Static data (background, hotspot
-- polygons + names) lives in lab_skull.yml; this file gives the hotspots behavior.
--
-- Hotspot handlers run as coroutine tasks, so talk() blocks line-to-line and the
-- player advances with a click. on_enter/on_exit are direct (non-blocking) setup
-- and cleanup, like a room's on_load/on_unload.
return {
  on_enter = function()
    -- The room beneath is frozen while this close-up is open, so an INSTANT room
    -- change here happens "off-screen" and is seen only when the player backs out.
    -- That is how we fake an entrance with no animation, e.g.:
    --   spawn_npc("schneider", "at_door", "down")
    -- (despawn_npc / set_state / show_object / hide_object work the same way.)
  end,

  on_exit = function()
    -- ...and the matching exit, e.g. despawn_npc("schneider").
  end,

  hotspots = {
    -- Click the big skull: Julia comments in a few steps, and we remember she read
    -- the inscription (the extra line is shown only the first time).
    craneo = function()
      talk("player", "Un cráneo casi intacto.")
      talk("player", "Hay una inscripción diminuta en la base.")
      if not get_state("lab.read_skull") then
        talk("player", "...'No me clasifiques'. Qué gracioso.")
        set_state("lab.read_skull", true)
      end
    end,

    huesos = function()
      talk("player", "Huesos sueltos. Tibias, costillas, un peroné con carácter.")
    end,
  },
}
