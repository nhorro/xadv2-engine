#include "engine/core/load_error.hpp"
#include "engine/core/strings.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace pac::core;
using pac::test::error_code;

namespace {

// A complete, valid strings file: every required block plus all ten default
// captions. Individual negative tests drop or corrupt one piece of this.
const char* kValid = R"YAML(
version: 1
language: es
verbs:
  look_at: "Mirar"
  use: "Usar"
connectors:
  use: "con"
  give: "a"
ui:
  walk_to: "Ir a"
defaults:
  cant_look_at: "No veo nada."
  cant_pick_up: "No puedo agarrarlo."
  wont_open: "No abre."
  wont_close: "No cierra."
  wont_push: "No empuja."
  wont_pull: "No tira."
  cant_use_that_way: "Así no."
  no_one_to_give_to: "A nadie."
  nothing_to_say: "Nada que decir."
  nothing_happens: "No pasa nada."
)YAML";

} // namespace

TEST_CASE("parse_strings reads blocks and caption() returns last-resort text") {
    const Strings s = parse_strings(kValid);
    CHECK(s.language == "es");
    CHECK(s.verb_label("look_at") == "Mirar");
    CHECK(s.connector("use") == "con");
    CHECK(s.ui_label("walk_to") == "Ir a");
    CHECK(s.caption("nothing_happens") == "No pasa nada.");
    CHECK(s.caption("no_one_to_give_to") == "A nadie.");
    CHECK(s.caption("missing") == "?missing"); // visible placeholder, not silent
}

TEST_CASE("parse_strings requires the core blocks") {
    auto drop = [](const std::string& block) {
        std::string y = kValid;
        const auto pos = y.find(block + ":");
        // Rename the block header to a harmless distinct key (keeps the YAML
        // valid) so the real block is absent and the loader reports it missing.
        y[pos] = 'z';
        return y;
    };
    CHECK(error_code([&] { parse_strings(drop("verbs")); }) == "strings.verbs-missing");
    CHECK(error_code([&] { parse_strings(drop("connectors")); }) == "strings.connectors-missing");
    CHECK(error_code([&] { parse_strings(drop("ui")); }) == "strings.ui-missing");
    CHECK(error_code([&] { parse_strings(drop("defaults")); }) == "strings.defaults-missing");
}

TEST_CASE("parse_strings rejects a defaults block missing a required key") {
    std::string y = kValid;
    const auto pos = y.find("  nothing_happens:");
    y.erase(pos, y.find('\n', pos) - pos);
    CHECK(error_code([&] { parse_strings(y); }) == "strings.defaults-missing-key");

    // The diagnostic pins a YAML line even though the test fed raw text (no file).
    try {
        parse_strings(y);
    } catch (const LoadError& e) {
        CHECK(e.location().line > 0);
        CHECK(e.location().file.empty());
    }
}

TEST_CASE("parse_strings rejects an unknown defaults key (likely a typo)") {
    std::string y = kValid;
    y += "  nothing_happenss: \"typo\"\n";
    CHECK(error_code([&] { parse_strings(y); }) == "strings.defaults-unknown-key");
}

TEST_CASE("parse_strings rejects malformed input") {
    CHECK(error_code([] { parse_strings("- just\n- a\n- list\n"); }) == "strings.root-not-map");
}
