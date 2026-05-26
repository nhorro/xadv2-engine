// room-sketches — a stripped-down launcher used as a playground for shader
// and gameplay experiments. Reuses the engine's standard scene factory
// (TitleScreen, RoomScene, SaveLoadScene, Cutscene, ...) so any feature the
// engine ships works inside the sandbox; the manifest just doesn't wire most
// of them. Run with:
//
//   pac_room_sketches                                # uses ./experiments/room_sketches/data/game.yaml
//   pac_room_sketches path/to/other/game.yaml        # any manifest
//   pac_room_sketches --frames 5 --shot out.png      # headless smoke
//   pac_room_sketches --pak resources.pak            # opt into a packed build

#include "engine/pnc/game_app.hpp"

#include <cstdlib>
#include <string>

namespace {

constexpr char kDefaultManifest[] = "experiments/room_sketches/data/game.yaml";

} // namespace

int main(int argc, char** argv) {
    std::string manifest = kDefaultManifest;
    pac::core::RunOptions opts;
    if (argc > 0 && argv[0]) {
        opts.argv0 = argv[0]; // hint for the pak-next-to-exe lookup (#109)
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            opts.max_frames = std::atoi(argv[++i]);
        } else if (arg.rfind("--frames=", 0) == 0) {
            opts.max_frames = std::atoi(arg.c_str() + 9);
        } else if (arg == "--shot" && i + 1 < argc) {
            opts.screenshot_path = argv[++i];
        } else if (arg.rfind("--shot=", 0) == 0) {
            opts.screenshot_path = arg.c_str() + 7;
        } else if (arg == "--pak" && i + 1 < argc) {
            opts.pak_path = argv[++i];
        } else if (arg.rfind("--pak=", 0) == 0) {
            opts.pak_path = arg.c_str() + 6;
        } else if (!arg.empty() && arg[0] != '-') {
            manifest = arg;
        }
    }

    return pac::pnc::run_game(manifest, opts);
}
