#include "engine/core/speech_config.hpp"

#include "engine/core/resource_source.hpp"

#include <array>

namespace pac::core {

std::string find_voice_resource(const ResourceSource& resources,
                                const SpeechConfig& config,
                                const std::string& text_id) {
    if (config.voice_directory.empty() || text_id.empty()) {
        return {};
    }
    constexpr std::array<const char*, 4> extensions = {".ogg", ".wav", ".flac", ".mp3"};
    for (const char* extension : extensions) {
        const std::string logical = config.voice_directory + "/" + text_id + extension;
        if (is_valid_logical_path(logical) && resources.exists(logical)) {
            return logical;
        }
    }
    return {};
}

} // namespace pac::core
