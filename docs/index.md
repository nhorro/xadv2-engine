# Extraordinary Adventures Engine

`xadv2-engine` is a C++20 / SFML 2D game engine with **Lua** scripting,
**YAML** data, and an optional point-and-click adventure kit.

This site covers two audiences: people changing the engine, and people
authoring a game against its Lua and YAML API.

<div class="grid cards" markdown>

-   :material-cog: __Engine__

    ---

    As-built architecture, implementation conventions, and separately labelled
    technical-debt plans.

    [:octicons-arrow-right-24: Engine](development/index.md)

-   :material-script-text: __Authoring API__

    ---

    Lua API, YAML data formats, chapter manifests, scenery/lighting fields,
    and the tools that emit that data. No C++ required.

    [:octicons-arrow-right-24: Authoring API](authoring/index.md)

</div>

## About this documentation

- Engine and authoring pages are in **English**.
- Content is Markdown under [`docs/`](https://github.com/nhorro/xadv2-engine/tree/develop/docs),
  rendered with [MkDocs](https://www.mkdocs.org/) + [Material](https://squidfunk.github.io/mkdocs-material/).

!!! note "As-built tour first"
    Engine developers start in the
    [architecture tour](development/tour/index.md). The older
    [design documents](development/history/design/00-index.md) are frozen history.
    Game authors start in the [Authoring API](authoring/index.md).

## Building this site

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r docs/requirements.txt

mkdocs serve          # live preview at http://127.0.0.1:8000
mkdocs build          # static HTML site into ./site/
```
