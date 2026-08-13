#pragma once

#include <string>
#include <vector>

namespace pac::core {

struct LanguageEntry;

/// Best-effort OS locale name (for example `es_AR.UTF-8` or `en-US`). An empty
/// result simply means detection was unavailable.
[[nodiscard]] std::string system_locale_name();

/// Select an initial manifest language from an OS locale. Exact locale ids win,
/// then the locale's primary language (`es` from `es_AR`), then English, then
/// the manifest default. This is used only when settings contain no saved
/// language preference.
[[nodiscard]] std::string select_initial_language(const std::vector<LanguageEntry>& languages,
                                                  const std::string& manifest_default,
                                                  const std::string& locale_name);

} // namespace pac::core
