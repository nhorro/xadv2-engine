return {
  on_enter = function()
     -- The room beneath is frozen while this close-up is open, so an INSTANT room
    -- change here happens "off-screen" and is seen only when the player backs out.
    -- That is how we fake an entrance with no animation, e.g.:
    --   spawn_npc("schneider", "at_door", "down")
    -- (despawn_npc / set_state / show_object / hide_object work the same way.)    
  end,

  on_exit = function()
  end,

  hotspots = {
  },
}