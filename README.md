Extrardinary Adventures Engine v2
=================================

This is a remake of the third person point and click engine [Extraordinary Adventures](https://github.com/nhorro/ea-engine).


Docker
------

Containerized environments to build/run the engine and the authoring tools on
Linux without installing the toolchain locally. See [docker/README.md](docker/README.md).

~~~bash
docker compose run --rm engine-test   # build the engine + run the headless tests
docker compose up room-editor         # then open http://localhost:8000
docker compose run --rm engine        # run the sample game (X11 + audio, desktop host)
~~~


Documentation
-------------

~~~bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r docs/requirements.txt
mkdocs serve
~~~