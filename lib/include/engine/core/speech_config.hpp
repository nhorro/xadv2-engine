#pragma once

#include <string>

namespace pac::core {

/// Game-wide presentation for spoken lines. UI typography remains owned by each
/// scene; this config is shared by every scene that renders `talk` / `remark`.
struct SpeechConfig {
    std::string font;
    unsigned font_size = 24;
};

} // namespace pac::core
