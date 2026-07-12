// A standard game needs no more C++ than this: everything lives in data/.
#include "engine/pnc/game_app.hpp"

int main(int argc, char** argv) {
    return pac::pnc::run_game_main(argc, argv, "examples/04_cutscene/data/game.yaml");
}
