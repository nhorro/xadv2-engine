#include "engine/core/manifest.hpp"
#include "engine/core/system_language.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace pac::core;

namespace {

std::vector<LanguageEntry> spanish_english() {
    return {{"es", "Español", "strings/es.yaml", {}}, {"en", "English", "strings/en.yaml", {}}};
}

} // namespace

TEST_CASE("Spanish system locale variants select Spanish") {
    const auto languages = spanish_english();
    CHECK(select_initial_language(languages, "es", "es") == "es");
    CHECK(select_initial_language(languages, "es", "es_AR.UTF-8") == "es");
    CHECK(select_initial_language(languages, "es", "ES-es") == "es");
    CHECK(select_initial_language(languages, "es", "es_MX@calendar") == "es");
}

TEST_CASE("non-Spanish and unknown locales fall back to English") {
    const auto languages = spanish_english();
    CHECK(select_initial_language(languages, "es", "en_US.UTF-8") == "en");
    CHECK(select_initial_language(languages, "es", "de-DE") == "en");
    CHECK(select_initial_language(languages, "es", "C") == "en");
    CHECK(select_initial_language(languages, "es", "") == "en");
}

TEST_CASE("supported future languages match before the English fallback") {
    auto languages = spanish_english();
    languages.push_back({"fr", "Français", "strings/fr.yaml", {}});
    CHECK(select_initial_language(languages, "es", "fr_CA.UTF-8") == "fr");
}

TEST_CASE("manifest default remains the fallback when English is unavailable") {
    const std::vector<LanguageEntry> languages = {{"es", "Español", "strings/es.yaml", {}},
                                                  {"fr", "Français", "strings/fr.yaml", {}}};
    CHECK(select_initial_language(languages, "es", "de-DE") == "es");
}
