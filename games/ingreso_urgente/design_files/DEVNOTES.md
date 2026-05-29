Ingreso Urgente - DEVNOTES
==========================


Issues encontrados

- Uso de objects en rooms. ¿Ejemplo? ¿El editor lo soporta?
- Diferencia entre objects y layers
- En el handler de un comando, sino se retorna algo en la función Lua, responde con la acción por defecto.



Cheatsheet
----------


~~~bash
PYTHONPATH=tools python3 -m tools.room_editor serve --room games/ingreso_urgente/data/rooms/hall.yaml --base-path games/ingreso_urgente/data/rooms
~~~