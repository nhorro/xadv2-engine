# Narrativa y puzzles

Cómo traducir la **escritura tradicional** (historia, personajes, escenas) al
lenguaje de una aventura point & click: **mecánicas, puzzles y diálogos** que el
jugador descubre interactuando.

!!! note "Estado"
    Esqueleto. Abajo está el índice de lo que cubriremos; el contenido se irá
    completando.

## De la escritura lineal a la interactiva

> **TODO:** la diferencia entre contar y dejar descubrir; cómo una escena escrita
> se convierte en una sala con hotspots, objetos y objetivos.

- **Escena → sala.** Cada escena se ancla en uno o varios lugares jugables.
- **Información → hotspots y diálogo.** Lo que en prosa es descripción, aquí se
  reparte entre lo que el jugador puede mirar, usar y preguntar.
- **Progreso → estado.** El avance se modela con *flags* persistentes (`set_state`),
  no con texto fijo.

## Diseño de puzzles

> **TODO:** taxonomía de puzzles (uso de objeto, combinación de inventario,
> diálogo, observación, secuencia), dificultad y *fair play* (pistas, lógica).

Principios de partida:

- Todo puzzle debe tener una **lógica deducible** dentro del mundo.
- Dar **realimentación** ante intentos razonables (verbos sobre hotspots).
- Evitar callejones sin salida: el estado nunca debería bloquear el final.

## Diálogos

> **TODO:** estructura de árboles de diálogo, ramas, opciones `once`, tono de
> personaje, y cómo se mapea a la API de diálogo del motor.

## Inventario y combinación

> **TODO:** qué objetos existen, cómo se obtienen, cómo se combinan, y qué historia
> cuentan.

## De la idea al contenido del juego

Una vez diseñada la historia y los puzzles, se implementan como **datos y
comportamiento**:

- *Qué existe* (salas, objetos, hotspots, inventario) → **YAML**.
- *Qué ocurre* (verbos, ramas de diálogo, acciones, flags) → **Lua**.

Ver la sección [Content Creators](../authoring/index.md) _(inglés)_ y, en
particular, la [referencia de la API de Lua](../authoring/lua-api.md).

## Ver también

- [Point & click concepts § hotspots, dialog, inventory](../development/design/04-point-and-click-concepts.md) _(inglés)_
- [Lua & content authoring guide](../development/coding-guide/lua-game.md) _(inglés)_
