#pragma once

#include <functional>
#include <string>

namespace pac::core {

struct EngineContext;
struct Manifest;
class SceneFactory;

struct RunOptions {
    /// 0 = run until quit. > 0 = render this many frames then exit (smoke test).
    int max_frames = 0;
    /// If set, save the final rendered frame to this host path (dev/debug capture).
    std::string screenshot_path;
    /// argv[0] from main(), used to derive the executable's directory for the
    /// `resources.pak` lookup (#109). Optional — empty leaves only the CWD as a
    /// candidate, and a path argument never falls through to the pak path
    /// anyway.
    std::string argv0;
    /// Explicit override of the pak location (`--pak`); when set, takes
    /// precedence over the exe/CWD lookup. Empty = auto-discover.
    std::string pak_path;
};

/// Optional compiled-game setup called after core Lua bindings are installed and
/// before the entry scene is constructed.
struct ApplicationHooks {
    std::function<void(EngineContext&, const Manifest&)> configure;
};

/// Parse the standard game command line into `opts` and return the manifest path
/// to run — the first positional argument, or `default_manifest` when there is
/// none. Recognized flags (both `--flag value` and `--flag=value` forms):
///
///     --frames N   render N frames, then exit (headless smoke)
///     --shot PATH  write the final frame to PATH
///     --pak PATH   load resources from this pak instead of auto-discovering one
///
/// `argv[0]` is recorded in `opts.argv0` for the pak-next-to-exe lookup. Every
/// game's `main` needs exactly this, so the engine owns it rather than having
/// each one copy it.
std::string parse_run_options(int argc,
                              char** argv,
                              RunOptions& opts,
                              const std::string& default_manifest);

/// Core harness: load the manifest, create services + window, and run the
/// fixed-timestep loop until the scene stack quits. The factory must already be
/// populated with the scene types the game needs (the core layer does not know
/// concrete genre scenes). Returns a process exit code.
int run(const std::string& manifest_path,
        const SceneFactory& factory,
        const RunOptions& opts = {},
        const ApplicationHooks& hooks = {});

} // namespace pac::core
