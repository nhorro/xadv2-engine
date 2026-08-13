# Localization and native-language voice

The source language stays where authors work: names and dialogue remain inline in
room YAML and Lua. Additional languages are lookup catalogs, so incomplete
translations always fall back to the authored source text.

## Manifest

```yaml
default_language: es
languages:
  - id: es
    name: Español
    strings: campaign/strings/es.yaml
  - id: en
    name: English
    strings: campaign/strings/en.yaml
    translations: campaign/translations/en.yaml

speech:
  font: fonts/dialogue.ttf
  font_size: 30
  voice_directory: speech/es

settings:
  audio:
    speech_enabled: true

development:
  warn_missing_translations: true
```

Each language has a complete engine-UI `strings` file. The source language
normally has no content catalog. An additional-language catalog is simple:

```yaml
version: 1
language: en
translations:
  dialog.malena.greeting.npc.1: "Hello."
  room.archive.hotspot.door.name: "entrance door"
  closeup.letter.intro: "This letter says that..."
```

## Text ids

The engine generates contextual ids where it already knows the content structure:

- dialog NPC line: `dialog.<dialog>.<node>.npc.<line>`
- dialog option: `dialog.<dialog>.<node>.option.<index>`
- room hotspot: `room.<room>.hotspot.<hotspot>.name`
- close-up hotspot: `closeup.<closeup>.hotspot.<hotspot>.name`
- inventory item: `inventory.<item>.name`
- cutscene slide: `cutscene.<cutscene>.slide.<slide-id-or-index>.text`
- map entry: `map.destination|reference|route.<entry>.name|description`
- notebook content: `notebook.section|note|objective.<entry>.<field>`
- case term: `case.term.<term>.name`

A dialog option may declare `id = "ask_about_letter"` beside its Spanish label;
the final id ends in `.option.ask_about_letter` and survives option reordering.

Free-form speech and captions accept an explicit id without moving the source
text:

```lua
talk("player", "Esta carta dice que...", { id = "closeup.letter.summary" })
show_text("Una semana antes...", 3.0, { id = "chapter.one.week_earlier" })
float_text("Cerrado", "door", { id = "room.archive.closed", duration = 2.0 })
```

When `talk`, `say`, or `remark` has no explicit id, the engine derives a readable
id from the source text, such as `text.que_vino_a_buscar.<suffix>`. This preserves
existing scripts and is useful during extraction. Prefer an explicit semantic id
for finalized content whose Spanish wording may still change. `tr(id, source)` is
available for other script-owned display text.

With `warn_missing_translations` enabled, each missing id is logged once while a
non-source language is active. The game continues with the source text.

## Documents

In-world document images can remain in their original language. Give Julia (or
the viewpoint character) localized narration that summarizes the relevant text:

```lua
talk("player", "Es una carta de Esteban Lamas...", {
  id = "closeup.f17_letter.summary",
})
```

This preserves the original artifact while making its evidence understandable to
international players, without maintaining language-specific image variants.

## Voice recordings

Voice acting is independent from subtitle language. `voice_directory: speech/es`
always selects the original Spanish performances, even when English subtitles are
active. Name a recording after its text id:

```text
speech/es/closeup.f17_letter.summary.ogg
```

The engine tries `.ogg`, `.wav`, `.flac`, and `.mp3`. When a file exists, the
subtitle remains for the recording's duration and dismissing the subtitle stops
the voice. When it is absent, speech behaves exactly as text-only speech did.
Players can enable or disable voices independently in Settings.
