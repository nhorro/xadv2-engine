-- =============================================================================
--  EXAMPLE DIALOG — a guided tour of the dialog system (for writers)
-- =============================================================================
--
--  This file is a worked example. Every line of game text is in Spanish (the
--  game's language); every EXPLANATION is an English comment beginning with `--`.
--  Comments are ignored by the game, so you can read them freely and delete the
--  ones you don't need once you understand the format.
--
--  ---------------------------------------------------------------------------
--  THE MENTAL MODEL — think of a conversation as a set of "beats"
--  ---------------------------------------------------------------------------
--  A dialog is a collection of NODES. Picture each node as one beat of the
--  conversation: the NPC says something, and the player is offered a few things
--  they can say back. Picking a reply jumps you to another node — that is how a
--  conversation "branches". The special word END finishes the conversation.
--
--      [intro] --> [hub] --pick a reply--> [some node] --> back to [hub] ...
--                    \--pick "Goodbye"---> END
--
--  Most conversations use a "hub" node: a central menu the player keeps coming
--  back to, with the topics fanning out from it. We use that shape below.
--
--  ---------------------------------------------------------------------------
--  HOW THIS DIALOG IS STARTED
--  ---------------------------------------------------------------------------
--  Somewhere in a room's Lua you (or a programmer) call:
--
--      start_dialog("delivery_guy")
--
--  The id "delivery_guy" does double duty: it is BOTH the name of this file
--  (dialogs/delivery_guy.lua) AND the cast character who speaks the `npc` lines.
--  That is why there is no "speaker" field anywhere in here — the engine already
--  knows the Repartidor is talking, and uses his voice colour and on-screen
--  position automatically. To make a dialog for a different character, copy this
--  file to dialogs/<that character's id>.lua.
--
--  ---------------------------------------------------------------------------
--  IDS vs. TEXT — keep them separate
--  ---------------------------------------------------------------------------
--  Words in quotes that the player or NPC SAYS  -> real text, can have accents,
--      spaces, punctuation: "¿Quién sos?"
--  Words used as node names (after `to =`, and the keys like `hub =`) are IDS:
--      internal labels. Keep them short, lowercase, no spaces or accents
--      (hub, ask_package, sign_form). The player never sees an id.
-- =============================================================================

return {

  -- `start` names the node the conversation opens on. Required.
  start = "intro",

  -- on_enter runs ONCE, the moment the dialog opens, before anything is shown.
  -- Use it for setup. Here we remember that Julia has met the Repartidor at all,
  -- so other parts of the game could react to it later. `set_state` is the game's
  -- long-term memory (see the note on state near the bottom of this file).
  on_enter = function()
    set_state("delivery.met", true)
  end,

  -- on_exit runs ONCE, when the conversation ends (whichever way it ends).
  -- Use it for cleanup. We leave it empty here; it is shown only to illustrate
  -- that the hook exists.
  on_exit = function()
  end,

  -- ===========================================================================
  --  NODES start here. Each remaining entry `name = { ... }` is one node.
  -- ===========================================================================

  -- The opening beat. A node with a `to` (and no `options`) is a "say something,
  -- then move on" node: the NPC speaks, then the conversation flows straight to
  -- the node named by `to` — no choice yet.
  intro = {
    npc = "Buenas. ¿Usted trabaja acá? Tengo una entrega y nadie me abre.",
    to = "hub",
  },

  -- The HUB: the central menu. A node with `options` offers the player a list of
  -- things to say. Each option is a small list: the FIRST element is the line the
  -- player says, and the named fields after it (to, when, run, once, silent)
  -- control what happens. `to` is the only required field on an option.
  hub = {
    -- A short NPC line shown above the menu each time we land on the hub. It is
    -- optional on an options node, but a one-liner keeps the scene alive.
    npc = "¿Y entonces? No tengo todo el día, el camión está en doble fila.",

    options = {

      -- (1) The simplest option: a player line and where it leads. Selecting it
      --     jumps to the "ask_package" node, which routes back here afterwards.
      { "¿Qué traés?", to = "ask_package" },

      -- (2) `once = true` makes an option disappear permanently after it is used.
      --     Good for one-time pleasantries or questions that only make sense to
      --     ask a single time. (This is remembered even across save/load.)
      { "¿Una mañana movida?", to = "small_talk", once = true },

      -- (3) `when` is a visibility rule: the option only appears if the function
      --     returns true. Here, "offer to sign" only shows up AFTER the player
      --     has asked what the package is (we set `delivery.knows_package` over in
      --     the ask_package node). This is how later topics "unlock".
      {
        "Dejámelo, yo lo firmo.",
        when = function()
          return get_state("delivery.knows_package") == true
        end,
        to = "sign_form",
      },

      -- (4) `run` is an action performed AT THE MOMENT the option is chosen, before
      --     jumping to its node. Use it to change the game: set a fact, give or
      --     take an inventory item, etc. Combined with `when`, this option appears
      --     only if the player is carrying a pen, and hands it over when picked.
      --
      --     NOTE: "pen" must be a real item id from inventory.yaml. If your game
      --     has no such item, just delete this whole option — it is here to show
      --     the has_item / remove_item pattern.
      {
        "Tomá, prestáte mi birome.",
        when = function()
          return has_item("pen") and not get_state("delivery.has_pen")
        end,
        run = function()
          remove_item("pen")              -- take the pen out of Julia's inventory
          set_state("delivery.has_pen", true)
        end,
        to = "thanks_pen",
      },

      -- (5) `silent = true` means the player's line is NOT spoken aloud — handy for
      --     an inner thought or a stage direction the player picks but Julia would
      --     not say out loud.
      { "(Mirar la credencial que cuelga de su cuello.)", silent = true, to = "badge" },

      -- (6) THE WAY OUT. Always keep at least one option that the player can pick
      --     no matter what — typically one that ends the talk with `to = END`.
      --     END is a special word (no quotes) that closes the conversation and
      --     returns control to normal play. Without a reachable exit, a player can
      --     get stuck if every other option is hidden by `when` or used up by
      --     `once`. This is the single most important rule of dialog writing.
      { "Pará, ya vuelvo.", to = END },
    },
  },

  -- ---------------------------------------------------------------------------
  --  CONTENT NODES. Each says something and routes back to the hub.
  -- ---------------------------------------------------------------------------

  -- `npc` can be a SINGLE line (as in `intro`) OR a LIST of lines, shown one after
  -- another. Use a list when a character rambles. The player reads each line in
  -- turn, then the menu (or the next node) appears.
  ask_package = {
    npc = {
      "Una caja. Pesada, sin remitente claro.",
      "Dice 'Instituto — Ingreso urgente' y nada más.",
      "A mí me pagan por dejarla, no por hacer preguntas.",
    },
    -- This is the unlock from option (3): now that Julia knows about the package,
    -- the "offer to sign" option will start appearing on the hub.
    run = function()
      set_state("delivery.knows_package", true)
    end,
    to = "hub",
  },

  small_talk = {
    npc = {
      "Ni me hable. Tercer reparto que nadie recibe.",
      "Parece que el mundo se tomó el día y olvidó avisarme.",
    },
    to = "hub",
  },

  badge = {
    -- A node reached from a `silent` option. The player thought something; here
    -- the NPC reacts to being looked at. Nodes don't care how they were reached.
    npc = "Sí, esa soy yo en la foto. Hace diez kilos y dos jefes atrás.",
    to = "hub",
  },

  thanks_pen = {
    npc = "Gracias. Una birome que escribe, milagro moderno.",
    to = "hub",
  },

  -- The pay-off node, reached only after the package topic is unlocked. It ends
  -- the conversation directly with `to = END` instead of returning to the hub —
  -- a node, just like an option, may route to END.
  sign_form = {
    npc = {
      "¿Segura? Firme acá, con eso me cubro.",
      "Listo. La caja queda en la entrada. Suerte con lo que sea que haya adentro.",
    },
    run = function()
      set_state("delivery.signed", true)  -- the rest of the game can react to this
    end,
    to = END,
  },
}

-- =============================================================================
--  REFERENCE — the few words you actually need
-- =============================================================================
--
--  NODE FIELDS
--    npc        What the character says: one "line" OR a { "list", "of", "lines" }.
--    options    A list of player replies (a menu). Use EITHER options OR to.
--    to         Where to go next: another node's id, or END to finish.
--    run        Code to run when this node is entered (set a fact, give an item…).
--
--  OPTION FIELDS  (an option is { "the player's line", field = value, ... })
--    [the text] (required) the line the player says — the first thing in the {}.
--    to         (required) the node this reply leads to, or END.
--    when       Show this reply only if the function returns true.
--    run        Do this the instant the reply is chosen.
--    once       true = the reply vanishes for good after being used.
--    silent     true = the player's line is not spoken aloud.
--
--  THE DIALOG'S MEMORY  (facts that must survive leaving/reopening or save/load)
--    set_state("name", value)   remember a fact (true/false, a number, or text).
--    get_state("name")          read it back ( == true is a handy explicit test).
--    Use dotted names to stay tidy: "delivery.signed", "delivery.knows_package".
--    Plain Lua variables do NOT persist — only set_state is remembered. Always
--    record progress with set_state, never with a local variable.
--
--  INVENTORY
--    has_item("id")     true if the player is carrying that item.
--    remove_item("id")  / add_item("id")  take / give an item.
--    Item ids come from inventory.yaml.
--
--  GOLDEN RULES
--    1. Always leave a reachable exit (an option `to = END`), or the player can
--       get stuck once `when`/`once` hide everything else.
--    2. Ids (node names, state keys, item ids) are lowercase, no spaces/accents.
--       Spoken text can be anything.
--    3. Record progress with set_state so it survives save/load; gate later
--       topics with `when` reading that state. That is the whole trick to making
--       a conversation feel like it remembers what was said.
-- =============================================================================
