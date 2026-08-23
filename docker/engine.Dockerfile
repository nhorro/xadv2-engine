# Build and run the C++ SFML engine + sample games on Linux (issue #125).
#
# Mirrors the CI / dev environment: Ubuntu 24.04 with the native dependencies
# needed to compile the engine's pinned modified SFML, plus Lua 5.4 and yaml-cpp
# from apt. The header-only deps (sol2, doctest) are vendored in-tree.
#
# The project is compiled at image-build time and the non-GUI doctest/CTest suite
# is run under a virtual X server as a gate: graphics tests still create SFML
# textures and need a GL context. The default command launches the sample game, which
# opens a window (X11) and plays audio; both the display and a PulseAudio/PipeWire
# socket are shared from the host by the compose `engine` service — see
# docker/README.md. `libpulse0` lets OpenAL's PulseAudio backend load at runtime
# (it dlopens libpulse). The test suite (engine-test service) is fully headless.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        pkg-config \
        libx11-dev \
        libxrandr-dev \
        libxcursor-dev \
        libudev-dev \
        libgl1-mesa-dev \
        libfreetype-dev \
        libopenal-dev \
        libflac-dev \
        libvorbis-dev \
        liblua5.4-dev \
        libyaml-cpp-dev \
        ca-certificates \
        libpulse0 \
        xvfb \
        xauth \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY . /work

# Configure + build (Release) and gate the image on the non-GUI test suite.
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)" \
    && xvfb-run -a ctest --test-dir build --output-on-failure --label-exclude gui

# Launch the sample game by default. Override to run another manifest or a
# headless smoke (append `--frames 5`). Showing a window requires X11.
CMD ["./build/examples/01_hello_room/pac_example_01_hello_room", "examples/01_hello_room/data/game.yaml"]
