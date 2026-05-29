# Arte y Narrativa

Sección para **artistas y guionistas** de aventuras point & click. Cubre cómo
diseñar lo que el jugador ve y juega: los fondos y sprites, y las historias y
puzzles que los conectan.

A diferencia de las secciones [Technical](../development/index.md) y
[Content Creators](../authoring/index.md) (en inglés), esta sección está escrita
en **español**, porque está pensada para el equipo creativo.

!!! info "Qué encontrarás aquí"
    - **[Fondos y sprites](backgrounds-sprites.md)** — conceptos, técnicas y
      prompts útiles para generar y preparar arte coherente con el motor.
    - **[Narrativa y puzzles](storytelling-puzzles.md)** — cómo traducir
      escritura tradicional a mecánicas, ideas de puzzles y diseño de historia.

## Cómo encaja con el resto

El arte y la escritura producen **assets** (imágenes, hojas de sprites) y
**contenido** (textos, diálogos, ideas de puzzle). Para llevarlos al juego:

1. Prepara las imágenes con las [herramientas](../authoring/tools/index.md)
   (transparentizar → empaquetar hoja de sprites → editor de salas).
2. Quien arme el contenido las conecta vía YAML y Lua siguiendo la sección
   [Content Creators](../authoring/index.md).

!!! note "Convención de assets"
    - Trabaja a la **resolución virtual** del juego (definida en el manifest).
    - Mantén separados `id` (ASCII, estable) y `name` (texto visible, con tildes).
    - Los assets se referencian por **ruta lógica** relativa a `resources.src`
      (p. ej. `backgrounds/study.png`).

---

> **TODO (esqueleto):** esta sección está por escribirse. Las páginas siguientes
> son el índice de lo que cubriremos.
