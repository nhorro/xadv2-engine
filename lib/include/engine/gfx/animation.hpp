#pragma once

#include <map>
#include <string>
#include <vector>

namespace pac::gfx {

struct FrameRef {
    std::string sprite; // frame id in the spritesheet
    float duration;     // seconds
};

struct Sequence {
    bool loop = false;
    bool h_mirror = false;
    std::vector<FrameRef> frames;
};

/// Parsed animation definition (`*.anim.yaml`): which spritesheet, the pivot
/// anchor name, and the named playable sequences. Headless and testable.
struct Animation {
    std::string spritesheet; // logical path, relative to the anim file's directory
    std::string pivot;       // anchor name used as the position pivot
    std::map<std::string, Sequence> sequences;

    const Sequence* sequence(const std::string& name) const;
    bool has(const std::string& name) const { return sequence(name) != nullptr; }
};

/// Parse animation YAML. Throws AssetError on malformed data.
Animation parse_animation(const std::string& yaml_text);

} // namespace pac::gfx
