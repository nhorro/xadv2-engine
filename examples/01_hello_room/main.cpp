// 01_hello_room — a standard game needs no more C++ than this. Everything the
// example does lives in data/: YAML for what exists, Lua for what happens.
#include "engine/pnc/game_app.hpp"

int main(int argc, char** argv) {
    // Parses --frames / --shot / --pak and an optional manifest argument.
    return pac::pnc::run_game_main(argc, argv, "examples/01_hello_room/data/game.yaml");
}
