# Extraordinary Adventures Engine

`xadv2-engine` is a ground-up rebuild of the **Extraordinary Adventures Engine**:
a C++17 / SFML engine for third-person, SCUMM-style point-and-click adventure
games, scripted in **Lua** and configured with **YAML**.

This documentation is organized around three audiences. Start with the section
that matches what you are trying to do.

<div class="grid cards" markdown>

-   :material-cog: __Technical__

    ---

    For **engine developers**. The canonical design documents (the source of
    truth for the engine) plus the C++ and content coding guides — how the engine
    is built and how to make decisions so the game can evolve.

    [:octicons-arrow-right-24: Go to Technical](development/index.md)

-   :material-script-text: __Content Creators__

    ---

    For **game authors**. The Lua scripting API reference, the YAML data-format
    reference, and how to use the asset/authoring tools. No C++ required.

    [:octicons-arrow-right-24: Go to Content Creators](authoring/index.md)

-   :material-palette: __Arte y Narrativa__ _(Español)_

    ---

    Para **artistas y guionistas**. Diseño de fondos y sprites (conceptos,
    técnicas, prompts) y escritura de historias y puzzles para aventuras
    point & click.

    [:octicons-arrow-right-24: Ir a Arte y Narrativa](art/index.md)

</div>

## About this documentation

- **Technical** and **Content Creators** are written in **English**.
- **Arte y Narrativa** is written in **Spanish**.
- All content is authored in Markdown under [`docs/`](https://github.com/nhorro/xadv2-engine/tree/develop/docs)
  and rendered with [MkDocs](https://www.mkdocs.org/) + the
  [Material](https://squidfunk.github.io/mkdocs-material/) theme. See
  [Building this site](#building-this-site).

!!! note "The design docs remain the source of truth"
    The implementation follows the design, not the other way around. Where code
    diverges from the [design documents](development/design/00-index.md), the code is
    what changes. This site renders those documents in place; it does not replace
    them.

## Building this site

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r docs/requirements.txt

mkdocs serve          # live preview at http://127.0.0.1:8000
mkdocs build          # static HTML site into ./site/
```

The standalone design **PDF** is still produced by
[`docs/development/design/build-pdf.sh`](https://github.com/nhorro/xadv2-engine/blob/develop/docs/development/design/build-pdf.sh)
(Markdown → HTML → headless-Chrome print), independent of MkDocs.
