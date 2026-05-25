# Fondos y sprites

Cómo diseñar el arte de una aventura point & click: los **fondos** de cada sala y
los **sprites** (personajes, objetos, animaciones). Esta página reúne conceptos,
técnicas y prompts útiles.

!!! note "Estado"
    Esqueleto. Abajo está el índice de lo que cubriremos; el contenido se irá
    completando.

## Conceptos

- **Resolución virtual y escala.** Todo el arte se diseña a la resolución del
  juego (manifest `resolution:`) y el motor lo escala con letterbox/pillarbox.
- **Capas de fondo (layers).** Una sala es un conjunto de capas: cielo, fondo,
  decorados, *occluders* (muebles que tapan al avatar). El orden lo da `z`.
- **Punto de apoyo (anchor "feet").** Los sprites de personaje se anclan por los
  pies para que el escalado y la profundidad funcionen.
- **Paleta y coherencia.** Mantener una paleta y un nivel de detalle consistentes
  entre salas.

## Diseñar un fondo

La resolución de diseño recomendada y establecida por defecto es 1280x720 píxels, de los cuáles un 15% se destina al panel de verbos + inventario.

El motor admite scrolling, y es recomendado usar esta capacidad, especialmente en el modo horizontal.

Para el diseño vertical, se recomienda que todo lo que es de interés en una escena esté dentro de los 612 píxeles (85%).

![Estructura de escena jugable](./assets/background-01.png)

### Lineamientos para composición de escenas interiores

![Composición de interiores](./assets/background-02.png)

### Lineamientos para composición de escenas exteriores

![Composición de exteriores](./assets/background-03.png)

## Diseñar sprites y animaciones

> **TODO:** convención de poses (parado/caminando × 4 direcciones), número de
> frames, tamaño relativo al fondo, anclas.

## Técnicas de generación (modelos de difusión)

Los generadores de imágenes (ChatGPT y otros) son un buen punto de partida, pero
su salida hay que **limpiarla**: no producen transparencia real, no respetan una
grilla fija y dejan artefactos.

Flujo recomendado:

1. Generar la imagen (fondo u hoja de sprites).
2. [Transparentizar](../authoring/tools/transparentizer.md) si necesitas alpha real.
3. [Empaquetar la hoja de sprites](../authoring/tools/spritesheet-packer.md) para
   obtener un atlas limpio + YAML.
4. Colocar las capas con el [editor de salas](../authoring/tools/room-editor.md).

### Prompts útiles

> **TODO:** colección de prompts probados para fondos (estilo, perspectiva,
> iluminación) y para hojas de sprites (grilla, fondo chroma, vista de personaje).

```text
# Plantilla de prompt (placeholder)
[estilo] background of [lugar], point-and-click adventure, [hora del día],
flat lighting, no characters, [paleta], wide composition with empty floor area
for the player to walk, [resolución] aspect.
```

## Checklist antes de integrar

- [ ] Resolución y proporción correctas.
- [ ] Transparencia real (sin halos) donde corresponde.
- [ ] Sprites recortados a un frame lógico por celda, con ancla de pies.
- [ ] Rutas lógicas e `id`s estables definidos.

## Ver también

- [Herramientas](../authoring/tools/index.md)
- [Generic 2D concepts](../sources/design/03-2d-game-concepts.md) _(inglés)_
- [Point & click concepts](../sources/design/04-point-and-click-concepts.md) _(inglés)_
