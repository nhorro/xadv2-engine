Guión de Ingreso Urgente
========================

Acto I
------

**Objetivos** 

- Introducir a Julia, su mundo, y su relación con la Dra Schneider.

### Desarrollo

Inicia con cut-scene animada con Julia mirando por la ventana que rompe la cuarta pared. Luego de dos segundos de mirar por la ventana, se acerca hacia el centro del cuarto mirando al jugador y lo saluda para presentarse. No dice su apellido (pero luego lo inferiremos porque la Dr. Schneider la llama por el apellido). Dice que es becaria, y que la Dr. Schneider la ha encargado tareas de clasificación.

Julia deja entrever que es una persona inquieta, curiosa, simpática, pero poco convergente. Dice que se tiene que poner a trabajar, pero que se distrae con facilidad, por ejemplo, mirando por la ventana, porque a veces se ven llamas y en el lugar de donde ella viene (la ciudad, pero no aclara cuál), eso no es habitual.
La animación continua con Julia diciendo que necesita música para trabajar, y se acerca a la radio que está sobre el escritorio para encenderla. Empieza a sonar una cumbia/chamamé que canta "Niña Inca - Seré tu Arqueólogo" con una música y letra pegadiza. Luego se acerca a la cocina con la pava para entenderla, se acuerda que tiene que vaciar el mate y el termo (que están al lado de la ventana), y cuando se acerca a la ventana ve que hay unas llamas y pasamos a un close-up de la ventana donde vemos las llamas. Mientras todo esto ocurre, seguimos escuchando la música, y la idea es que eso genere una situación de un humor sutíl, porque la letra de la canción es intencionalmente boba, pero totalmente factible, sin llegar a la parodía.

Aprovechamos el close-up para hacer el spawn de la Dra. Schneider (así evitamos animar la puerta y no queda tan brusco que aparezca un personaje de la nada), que interrumpe mientras Julia ve las llamas y las alpacas, diciendo que espera los resultados. La Dr. Schneider se queja de la música y se acerca a la radio y la apaga.  Allí comienza el primer puzzle, que consiste en examinar cuatro evidencias en la escena:

- Un cráneo y huesos en la mesa
- Una pizarra con datos de una excavación en La Matilde
- Una ficha osteológica preliminar de un resto hallado en La Matilde.
- Un registro contextual del hallazgo en La Matilde.

Cada evidencia es un close-up, y al hacer click en partes de la imagen se establecen variables que habilitan nuevas opciones del diálogo con la Dra. Schneider, que permiten resolver el caso.

La Dr. Schneider muestra un trato lejano, propio de una emidencia académica hacia una estudiante, y tiene un lenguaje que expresa sus ideas con precisión, y también la demanda. Eso establece también la relación de la Dr. Schenider como alguien fría, distante, pero también competente (una especie de Dr. House).

### Puzzle 1 - El cadáver

~~~
-- close-ups vistos
evidence.bones_seen
evidence.board_seen
evidence.osteology_seen
evidence.context_seen

-- hallazgos comprendidos
finding.radial_fractures
finding.no_cut_marks
finding.no_collapse
finding.primary_burial
finding.heavy_lithic_object
finding.perimortem_possible

-- argumentos dichos en diálogo
argument.fracture_pattern_stated
argument.no_cut_marks_stated
argument.no_collapse_stated
argument.perimortem_stated

-- resolución
case.blunt_trauma_supported
case.report_ready
~~~

### Cierre