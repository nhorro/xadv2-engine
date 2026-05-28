# Build and run the C++ SFML engine + sample games on Linux (issue #125).
#
# Mirrors the CI / dev environment: Ubuntu 24.04 with system SFML 2.6, Lua 5.4,
# and yaml-cpp from apt. The header-only deps (sol2, doctest) are vendored
# in-tree, so the image needs no extra network fetch beyond apt.
#
# The project is compiled at image-build time and the headless doctest/CTest
# suite is run as a gate. The default command launches the sample game, which
# opens a window and therefore needs an X11 display shared from the host — see
# docker/README.md. The test suite (engine-test service) is fully headless.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        libsfml-dev \
        liblua5.4-dev \
        libyaml-cpp-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY . /work

# Configure + build (Release) and gate the image on the headless test suite.
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)" \
    && ctest --test-dir build --output-on-failure

# Launch the sample game by default. Override to run another manifest or a
# headless smoke (append `--frames 5`). Showing a window requires X11.
CMD ["./build/games/themummy/pac_themummy", "games/themummy/data/game.yaml"]
