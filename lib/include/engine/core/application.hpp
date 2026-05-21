#pragma once

#include <string>

namespace pac::core {

class SceneFactory;

struct RunOptions {
    /// 0 = run until quit. > 0 = render this many frames then exit (smoke test).
    int max_frames = 0;
};

/// Core harness: load the manifest, create services + window, and run the
/// fixed-timestep loop until the scene stack quits. The factory must already be
/// populated with the scene types the game needs (the core layer does not know
/// concrete genre scenes). Returns a process exit code.
int run(const std::string& manifest_path, const SceneFactory& factory, const RunOptions& opts = {});

} // namespace pac::core
