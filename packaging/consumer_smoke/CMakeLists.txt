# A minimal external consumer: exactly what a game's CMakeLists looks like when it
# links an INSTALLED engine. It is deliberately NOT part of the engine build — it
# is configured against an install prefix, which is the only way to prove that
# `cmake --install` produced something usable:
#
#   cmake --install build --prefix /tmp/xadv2-prefix
#   cmake -S packaging/consumer_smoke -B /tmp/consumer-build \
#         -DCMAKE_PREFIX_PATH=/tmp/xadv2-prefix
#   cmake --build /tmp/consumer-build
#
# scripts/check-packaging.sh does all of the above; CI runs it.
cmake_minimum_required(VERSION 3.16)
project(pac_consumer_smoke LANGUAGES CXX)

find_package(pac_engine CONFIG REQUIRED)

# The data-only game: engine, nothing else.
add_executable(consumer_smoke main.cpp)
target_link_libraries(consumer_smoke PRIVATE pac::engine)

# The game that extends the engine in C++: also links pac::sol2, so that
# engine/core/scripting_sol.hpp resolves. If the package forgot to ship sol2's
# headers, THIS is the target that fails.
add_executable(consumer_smoke_cpp main_cpp_scene.cpp)
target_link_libraries(consumer_smoke_cpp PRIVATE pac::engine pac::sol2)
