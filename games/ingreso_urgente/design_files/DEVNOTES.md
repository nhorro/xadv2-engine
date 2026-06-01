Ingreso Urgente - DEVNOTES
==========================


Issues encontrados

- En el handler de un comando, sino se retorna algo en la función Lua, responde con la acción por defecto.
- Closeups



Cheatsheet
----------

Run room editor 

From root:

~~~bash
PYTHONPATH=tools python3 -m tools.room_editor serve --room games/ingreso_urgente/data/rooms/hall.yaml --base-path games/ingreso_urgente/data/rooms
~~~

Run closeup editor 

From root:

~~~bash
PYTHONPATH=tools python3 -m tools.closeup_editor serve \
  --closeup games/ingreso_urgente/data/closeups/lab_skull.yml
~~~