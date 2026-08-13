#include "engine/core/text_id.hpp"

#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace pac::core {

namespace {
constexpr std::size_t kSlugLimit = 48;

char latin_ascii(unsigned codepoint) {
    switch (codepoint) {
    case 0x00c1:
    case 0x00e1:
    case 0x00c0:
    case 0x00e0:
    case 0x00c4:
    case 0x00e4:
        return 'a';
    case 0x00c9:
    case 0x00e9:
    case 0x00c8:
    case 0x00e8:
    case 0x00cb:
    case 0x00eb:
        return 'e';
    case 0x00cd:
    case 0x00ed:
    case 0x00cc:
    case 0x00ec:
    case 0x00cf:
    case 0x00ef:
        return 'i';
    case 0x00d3:
    case 0x00f3:
    case 0x00d2:
    case 0x00f2:
    case 0x00d6:
    case 0x00f6:
        return 'o';
    case 0x00da:
    case 0x00fa:
    case 0x00d9:
    case 0x00f9:
    case 0x00dc:
    case 0x00fc:
        return 'u';
    case 0x00d1:
    case 0x00f1:
        return 'n';
    case 0x00c7:
    case 0x00e7:
        return 'c';
    default:
        return 0;
    }
}

unsigned next_codepoint(const std::string& text, std::size_t& i) {
    const auto first = static_cast<unsigned char>(text[i++]);
    if (first < 0x80)
        return first;
    if ((first & 0xe0) == 0xc0 && i < text.size()) {
        return ((first & 0x1f) << 6) | (static_cast<unsigned char>(text[i++]) & 0x3f);
    }
    if ((first & 0xf0) == 0xe0 && i + 1 < text.size()) {
        const unsigned cp = ((first & 0x0f) << 12) |
                            ((static_cast<unsigned char>(text[i]) & 0x3f) << 6) |
                            (static_cast<unsigned char>(text[i + 1]) & 0x3f);
        i += 2;
        return cp;
    }
    while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80)
        ++i;
    return 0;
}

std::string slug(const std::string& text) {
    std::string out;
    bool separator = false;
    for (std::size_t i = 0; i < text.size() && out.size() < kSlugLimit;) {
        const unsigned cp = next_codepoint(text, i);
        const char c = cp < 128 ? static_cast<char>(cp) : latin_ascii(cp);
        if (c != 0 && std::isalnum(static_cast<unsigned char>(c))) {
            if (separator && !out.empty() && out.size() < kSlugLimit)
                out.push_back('_');
            if (out.size() < kSlugLimit) {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            separator = false;
        } else {
            separator = true;
        }
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    return out.empty() ? "line" : out;
}

std::uint32_t fnv1a(const std::string& text) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char c : text) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}
} // namespace

std::string text_id(const std::string& explicit_id, const std::string& source_text) {
    if (!explicit_id.empty())
        return explicit_id;
    std::ostringstream suffix;
    suffix << std::hex << std::setw(8) << std::setfill('0') << fnv1a(source_text);
    return "text." + slug(source_text) + "." + suffix.str();
}

} // namespace pac::core
