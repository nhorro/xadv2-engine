// The whole of a standard game's main(), compiled against the INSTALLED engine.
// If this builds, `find_package(pac_engine)` gives a game everything it needs.
#include "engine/pnc/game_app.hpp"

int main(int argc, char** argv) {
    return pac::pnc::run_game_main(argc, argv, "data/game.yaml");
}
