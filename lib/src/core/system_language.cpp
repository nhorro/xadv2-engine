#include "engine/core/system_language.hpp"

#include "engine/core/manifest.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <locale>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pac::core {

namespace {

std::string normalized_locale(std::string value) {
    const std::size_t suffix = value.find_first_of(".@");
    if (suffix != std::string::npos) {
        value.erase(suffix);
    }
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        if (c == '_') {
            return '-';
        }
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string_view primary_language(const std::string& normalized) {
    return std::string_view(normalized).substr(0, normalized.find('-'));
}

} // namespace

std::string system_locale_name() {
#if defined(_WIN32)
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    const int length = GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH);
    if (length > 1) {
        const int bytes =
            WideCharToMultiByte(CP_UTF8, 0, name, length - 1, nullptr, 0, nullptr, nullptr);
        if (bytes > 0) {
            std::string result(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8,
                                0,
                                name,
                                length - 1,
                                result.data(),
                                bytes,
                                nullptr,
                                nullptr);
            return result;
        }
    }
#else
    // POSIX precedence. Reading the environment avoids changing the process-wide
    // C locale merely to inspect it.
    for (const char* variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        if (const char* value = std::getenv(variable); value && *value) {
            return value;
        }
    }
#endif
    try {
        return std::locale("").name();
    } catch (const std::exception&) {
        return {};
    }
}

std::string select_initial_language(const std::vector<LanguageEntry>& languages,
                                    const std::string& manifest_default,
                                    const std::string& locale_name) {
    if (languages.empty()) {
        return manifest_default;
    }

    const std::string locale = normalized_locale(locale_name);
    const std::string_view primary = primary_language(locale);
    if (!locale.empty() && locale != "c" && locale != "posix") {
        for (const LanguageEntry& language : languages) {
            if (normalized_locale(language.id) == locale) {
                return language.id;
            }
        }
        for (const LanguageEntry& language : languages) {
            if (primary_language(normalized_locale(language.id)) == primary) {
                return language.id;
            }
        }
    }

    // English is the broad fallback while games commonly ship Spanish source
    // text plus one international translation. If English is unavailable, keep
    // the authored manifest default instead.
    for (const LanguageEntry& language : languages) {
        if (primary_language(normalized_locale(language.id)) == "en") {
            return language.id;
        }
    }
    for (const LanguageEntry& language : languages) {
        if (language.id == manifest_default) {
            return language.id;
        }
    }
    return languages.front().id;
}

} // namespace pac::core
