#pragma once

#include <string>

namespace pac::core {

class ResourceSource;

/// Game-wide presentation for spoken lines. UI typography remains owned by each
/// scene; this config is shared by every scene that renders `talk` / `remark`.
struct SpeechConfig {
    std::string font;
    unsigned font_size = 24;
    // Optional directory containing native-language voice files named after a
    // text id, e.g. speech/es/dialog.malena.start.npc.1.ogg.
    std::string voice_directory;
};

/// First existing native voice resource for `text_id`, trying the engine's
/// supported short-audio extensions. Empty means no recorded line; this is the
/// normal fallback while voice production is incomplete.
[[nodiscard]] std::string find_voice_resource(const ResourceSource& resources,
                                              const SpeechConfig& config,
                                              const std::string& text_id);

} // namespace pac::core
