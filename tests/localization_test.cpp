#include "engine/core/diagnostics.hpp"
#include "engine/core/localization.hpp"
#include "engine/core/manifest.hpp" // LanguageEntry
#include "engine/core/resource_source.hpp"

#include <doctest/doctest.h>

#include <map>
#include <string>
#include <vector>

using namespace pac::core;

namespace {

// A complete, valid UI-strings file (parse_strings requires every block plus all
// last-resort caption keys). `settings_label` is varied per language so a swap is
// observable through strings().ui_label("settings").
std::string strings_yaml(const std::string& lang, const std::string& settings_label) {
    return "language: " + lang +
           "\nverbs: { look_at: a }\nconnectors: { use: b }\nui: { settings: \"" + settings_label +
           "\" }\ndefaults:\n"
           "  cant_look_at: x\n  cant_pick_up: x\n  wont_open: x\n  wont_close: x\n"
           "  wont_push: x\n  wont_pull: x\n  cant_use_that_way: x\n  no_one_to_give_to: x\n"
           "  nothing_to_say: x\n  nothing_happens: x\n";
}

class MapSource : public ResourceSource {
public:
    std::map<std::string, std::string> files;

    bool exists(const std::string& logical) const override { return files.contains(logical); }
    std::string read_text(const std::string& logical) const override {
        const auto it = files.find(logical);
        if (it == files.end()) {
            throw ResourceError("missing: " + logical);
        }
        return it->second;
    }
    std::vector<std::byte> read_bytes(const std::string& /*logical*/) const override { return {}; }
};

std::vector<LanguageEntry> two_langs() {
    return {{"es", "Español", "es.yaml"}, {"en", "English", "en.yaml"}};
}

} // namespace

TEST_CASE("Localization loads the active language and lists choices") {
    MapSource src;
    src.files["es.yaml"] = strings_yaml("es", "Opciones");
    src.files["en.yaml"] = strings_yaml("en", "Options");
    Diagnostics log;

    Localization loc(src, two_langs(), "es", "en", log);
    CHECK(loc.active() == "en");
    CHECK(loc.active_index() == 1);
    CHECK(loc.has_choices());
    CHECK(loc.strings().ui_label("settings") == "Options");
}

TEST_CASE("set_language swaps the active strings in place") {
    MapSource src;
    src.files["es.yaml"] = strings_yaml("es", "Opciones");
    src.files["en.yaml"] = strings_yaml("en", "Options");
    Diagnostics log;

    Localization loc(src, two_langs(), "es", "es", log);
    CHECK(loc.strings().ui_label("settings") == "Opciones");

    CHECK(loc.set_language("en"));
    CHECK(loc.active() == "en");
    CHECK(loc.strings().ui_label("settings") == "Options");

    CHECK_FALSE(loc.set_language("en")); // already active
    CHECK_FALSE(loc.set_language("fr")); // unknown -> kept
    CHECK(loc.active() == "en");
    CHECK(loc.strings().ui_label("settings") == "Options");
}

TEST_CASE("Localization falls back to the default when the active language is unknown") {
    MapSource src;
    src.files["es.yaml"] = strings_yaml("es", "Opciones");
    Diagnostics log;

    Localization loc(src, {{"es", "Español", "es.yaml"}}, "es", "zz", log);
    CHECK(loc.active() == "es");
    CHECK_FALSE(loc.has_choices());
    CHECK(loc.strings().ui_label("settings") == "Opciones");
}

TEST_CASE("Localization falls back to the default when the active language fails to load") {
    MapSource src;
    src.files["es.yaml"] = strings_yaml("es", "Opciones");
    // en.yaml deliberately absent.
    Diagnostics log;

    Localization loc(src, two_langs(), "es", "en", log);
    CHECK(loc.active() == "es");
    CHECK(loc.strings().ui_label("settings") == "Opciones");
}

TEST_CASE("Localization rethrows when neither active nor default strings load") {
    MapSource src; // empty: nothing resolves
    Diagnostics log;
    CHECK_THROWS(Localization(src, two_langs(), "es", "en", log));
}
