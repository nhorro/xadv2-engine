Extrardinary Adventures Engine v2
=================================

This is a remake of the third person point and click engine [Extraordinary Adventures](https://github.com/nhorro/ea-engine).


Build instructions
------------------

### Linux

~~~bash
./build-linux.sh
~~~

### Windows

Requires a [vcpkg](https://github.com/microsoft/vcpkg) checkout. Set `VCPKG_ROOT`
to it (the script also auto-detects a sibling `..\vcpkg`), then:

~~~bat
.\build-windows.bat            REM Debug; pass Release for an optimized build
~~~

This stamps the vcpkg baseline, vcpkg-installs SFML 2.6 / yaml-cpp / Lua 5.4
(sol2 and doctest stay header-only), and builds the sample. For the IDE / CMake
preset flow that CI uses (`cmake --preset windows-msvc`), see the Windows section
of [CLAUDE.md](CLAUDE.md).

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
mkdocs serve -a localhost:8002 # Room/closeup editors use 8000/8001 by default
~~~