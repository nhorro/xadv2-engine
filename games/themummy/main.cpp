#include "engine/pnc/game_app.hpp"

#include <cstdlib>
#include <string>

namespace {

constexpr char kDefaultManifest[] = "games/themummy/data/game.yaml";

} // namespace

// Minimal game entry point: select a manifest and run. A standard game needs no
// more C++ than this; behavior lives in YAML + Lua (later milestones).
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
