# Sound effects

One-shot sound effects, referenced from Lua by logical path (e.g.
`play_sound("sfx/door_open.ogg")`). See design `05-scripting-api.md` §Audio.

Format: SFML decodes **WAV, OGG/Vorbis, FLAC, and MP3**. For short effects prefer
**WAV or OGG** (instant decode). Use a **mono** file if you want `play_sound`'s
`pan` argument to position it — stereo clips play as-authored and ignore panning.

The sample's door scripts expect these files (drop them here; a missing file just
logs a warning and is skipped):

| File | Played when |
|------|-------------|
| `door_open.ogg`   | a door is opened (study, exterior, and the unlocked hall exit) |
| `door_locked.ogg` | the locked hall exit is tried without the key |
| `door_unlock.ogg` | the key is used on the hall exit |
