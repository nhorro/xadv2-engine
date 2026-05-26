#include "engine/core/diagnostics.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/save_service.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

using pac::core::Diagnostics;
using pac::core::GameState;
using pac::core::LogLevel;
using pac::core::SaveService;
using pac::core::StateValue;

namespace {

Diagnostics quiet() {
    return Diagnostics(LogLevel::ERROR);
}

/// Per-test temp dir under the system temp path. Cleaned up on destruction.
struct TempDir {
    std::filesystem::path path;

    TempDir() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("pac_save_test_" + std::to_string(now));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

/// Build a GameState exercising every field type so the round-trip test
/// catches any encoder/decoder asymmetry.
GameState make_rich_state() {
    GameState s;
    s.save_version = 1;
    s.current_scene_id = "room_view";
    s.room_view.current_room_id = "study";
    s.room_view.player = {600.5f, 830.25f, "left", "julia_body"};
    s.inventory = {"notebook", "key"};
    s.global_state["stan.received_notebook"] = true;
    s.global_state["__dialog.stan.greet.3"] = true;
    s.global_state["mummy.awake"] = false;
    s.global_state["score"] = 42.5;
    s.global_state["turn_count"] = 7.0; // integer-valued double
    s.global_state["greeting"] = std::string("Hola, mundo");
    s.room_state["study"]["visited"] = true;
    s.room_state["hall"]["scratch"] = std::string("anything");
    s.region_states["hall"]["drawer"] = std::string("open");
    s.region_states["exterior"]["door"] = std::string("closed");
    s.hotspot_enabled["study"]["notebook"] = false; // picked up + disabled
    s.hotspot_enabled["hall"]["salida"] = true;
    s.object_visible["hall"]["cart"] = false; // pushed away
    s.object_visible["study"]["lamp"] = true;
    s.layer_visible["hall"]["cart_layer"] = false; // perspective cart removed
    s.layer_visible["study"]["blinds"] = true;
    return s;
}

} // namespace

TEST_CASE("save -> load round-trip preserves every field") {
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);

    const GameState in = make_rich_state();
    REQUIRE(svc.save(1, in));
    REQUIRE(svc.slot_exists(1));

    const auto out_opt = svc.load(1);
    REQUIRE(out_opt.has_value());
    const GameState& out = *out_opt;

    CHECK(out.save_version == 1);
    CHECK(out.current_scene_id == "room_view");
    CHECK(out.room_view.current_room_id == "study");
    CHECK(out.room_view.player.x == doctest::Approx(600.5f));
    CHECK(out.room_view.player.y == doctest::Approx(830.25f));
    CHECK(out.room_view.player.facing == "left");
    CHECK(out.room_view.player.appearance_id == "julia_body");

    REQUIRE(out.inventory.size() == 2);
    CHECK(out.inventory[0] == "notebook");
    CHECK(out.inventory[1] == "key");

    CHECK(std::get<bool>(out.global_state.at("stan.received_notebook")) == true);
    CHECK(std::get<bool>(out.global_state.at("__dialog.stan.greet.3")) == true);
    CHECK(std::get<bool>(out.global_state.at("mummy.awake")) == false);
    CHECK(std::get<double>(out.global_state.at("score")) == doctest::Approx(42.5));
    CHECK(std::get<double>(out.global_state.at("turn_count")) == doctest::Approx(7.0));
    CHECK(std::get<std::string>(out.global_state.at("greeting")) == "Hola, mundo");

    CHECK(std::get<bool>(out.room_state.at("study").at("visited")) == true);
    CHECK(std::get<std::string>(out.room_state.at("hall").at("scratch")) == "anything");

    CHECK(out.region_states.at("hall").at("drawer") == "open");
    CHECK(out.region_states.at("exterior").at("door") == "closed");

    CHECK(out.hotspot_enabled.at("study").at("notebook") == false);
    CHECK(out.hotspot_enabled.at("hall").at("salida") == true);
    CHECK(out.object_visible.at("hall").at("cart") == false);
    CHECK(out.object_visible.at("study").at("lamp") == true);
    CHECK(out.layer_visible.at("hall").at("cart_layer") == false);
    CHECK(out.layer_visible.at("study").at("blinds") == true);
}

TEST_CASE("load() returns nullopt on missing slot") {
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);
    CHECK_FALSE(svc.load(2).has_value());
    CHECK_FALSE(svc.load(SaveService::kAutosaveSlot).has_value());
}

TEST_CASE("save/load reject out-of-range slots without throwing") {
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);
    const GameState s = make_rich_state();
    CHECK_FALSE(svc.save(-1, s));
    CHECK_FALSE(svc.save(SaveService::kSlotCount, s));
    CHECK_FALSE(svc.load(-1).has_value());
    CHECK_FALSE(svc.load(99).has_value());
}

TEST_CASE("latest_slot picks the most recently written slot") {
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);

    CHECK_FALSE(svc.latest_slot().has_value());

    GameState s = make_rich_state();
    REQUIRE(svc.save(1, s));
    auto only = svc.latest_slot();
    REQUIRE(only.has_value());
    CHECK(*only == 1);

    // mtime granularity on some filesystems is coarse; sleep a bit so the next
    // file gets a strictly later timestamp.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(svc.save(3, s));
    auto latest = svc.latest_slot();
    REQUIRE(latest.has_value());
    CHECK(*latest == 3);

    // Autosave (slot 0) is just another slot for latest_slot's purposes.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(svc.save(SaveService::kAutosaveSlot, s));
    auto winner = svc.latest_slot();
    REQUIRE(winner.has_value());
    CHECK(*winner == SaveService::kAutosaveSlot);
}

TEST_CASE("string values that look like bool/number round-trip as strings") {
    // Regression guard: yaml-cpp's default scalar parsing turns unquoted
    // `true` / `42` into bool / int. The encoder must emit strings
    // double-quoted so the decoder reads them back as std::string.
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);

    GameState in;
    in.save_version = 1;
    in.current_scene_id = "room_view";
    in.global_state["looks_bool"] = std::string("true");
    in.global_state["looks_number"] = std::string("42");
    in.global_state["mixed"] = std::string("hello world");

    REQUIRE(svc.save(1, in));
    const auto out_opt = svc.load(1);
    REQUIRE(out_opt.has_value());

    const auto& g = out_opt->global_state;
    REQUIRE(std::holds_alternative<std::string>(g.at("looks_bool")));
    CHECK(std::get<std::string>(g.at("looks_bool")) == "true");
    REQUIRE(std::holds_alternative<std::string>(g.at("looks_number")));
    CHECK(std::get<std::string>(g.at("looks_number")) == "42");
    REQUIRE(std::holds_alternative<std::string>(g.at("mixed")));
    CHECK(std::get<std::string>(g.at("mixed")) == "hello world");
}

TEST_CASE("load() rejects an unsupported save_version") {
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);
    GameState s = make_rich_state();
    s.save_version = 999;
    REQUIRE(svc.save(1, s));
    CHECK_FALSE(svc.load(1).has_value());
}

TEST_CASE("stage_restore + take_pending_restore hand off a GameState once") {
    // Continue path: TitleScreen stages a loaded GameState; RoomScene::enter
    // consumes it via take_pending_restore. The slot is single-shot so a
    // second scene transition (e.g. quit-to-title-then-new-game) doesn't
    // accidentally re-apply the stale state.
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);

    CHECK_FALSE(svc.has_pending_restore());
    CHECK_FALSE(svc.take_pending_restore().has_value());

    svc.stage_restore(make_rich_state());
    CHECK(svc.has_pending_restore());

    auto taken = svc.take_pending_restore();
    REQUIRE(taken.has_value());
    CHECK(taken->current_scene_id == "room_view");
    CHECK(taken->room_view.current_room_id == "study");

    CHECK_FALSE(svc.has_pending_restore());
    CHECK_FALSE(svc.take_pending_restore().has_value());
}

TEST_CASE("description + saved_at round-trip; saved_at stamped on save (issue #108)") {
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);

    GameState in = make_rich_state();
    in.description = "Antes del Sarc\xc3\xb3"
                     "fago"; // "Antes del Sarcófago" in UTF-8
    in.saved_at = 0;         // unset; service must stamp

    const auto before = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    REQUIRE(svc.save(2, in));
    const auto after = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

    const auto out_opt = svc.load(2);
    REQUIRE(out_opt.has_value());
    CHECK(out_opt->description == in.description);
    // Stamped within the test window (allow a 2s slack for clock drift on slow CI).
    CHECK(out_opt->saved_at >= before - 2);
    CHECK(out_opt->saved_at <= after + 2);

    // The light header read returns the same metadata without parsing the rest.
    const auto summary = svc.slot_summary(2);
    REQUIRE(summary.has_value());
    CHECK(summary->description == in.description);
    CHECK(summary->saved_at == out_opt->saved_at);
}

TEST_CASE("slot_summary returns nullopt for a missing slot") {
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);
    CHECK_FALSE(svc.slot_summary(1).has_value());
}

TEST_CASE("pending_snap stages + drains exactly once") {
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);

    CHECK_FALSE(svc.has_pending_snap());
    CHECK_FALSE(svc.take_pending_snap().has_value());

    GameState in = make_rich_state();
    in.description = "snap";
    svc.stage_pending_snap(in);
    CHECK(svc.has_pending_snap());

    const auto out = svc.take_pending_snap();
    REQUIRE(out.has_value());
    CHECK(out->description == "snap");
    CHECK_FALSE(svc.has_pending_snap()); // single-shot, like stage_restore
}

TEST_CASE("save writes a thumbnail sidecar PNG when one is supplied (issue #119)") {
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);

    // 4×3 pseudo-image: just enough to round-trip via PNG (sf::Image::saveToFile
    // / loadFromFile decode through stb_image and may renormalize, but the path
    // shape is what we're verifying — not pixel exactness).
    sf::Image img;
    img.create(4u, 3u, sf::Color(120, 80, 40, 255));

    const GameState in = make_rich_state();
    REQUIRE(svc.save(1, in, &img));
    REQUIRE(svc.slot_exists(1));
    REQUIRE(svc.slot_has_thumbnail(1));

    sf::Image roundtrip;
    REQUIRE(roundtrip.loadFromFile(svc.thumbnail_path(1).string()));
    CHECK(roundtrip.getSize() == sf::Vector2u(4u, 3u));
}

TEST_CASE("save without a thumbnail leaves the slot's sidecar untouched") {
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);

    sf::Image img;
    img.create(2u, 2u, sf::Color::Red);
    const GameState in = make_rich_state();
    REQUIRE(svc.save(1, in, &img));
    REQUIRE(svc.slot_has_thumbnail(1));

    // Re-save the same slot without passing a thumbnail. The save should
    // succeed and the existing sidecar should still be on disk — clearing it
    // would mean "Guardar" silently loses the preview, which is the wrong UX.
    REQUIRE(svc.save(1, in, nullptr));
    CHECK(svc.slot_has_thumbnail(1));
}

TEST_CASE("stage_pending_thumbnail drains exactly once") {
    Diagnostics log = quiet();
    TempDir td;
    SaveService svc(td.path, log);

    sf::Image img;
    img.create(8u, 8u, sf::Color::Blue);
    svc.stage_pending_thumbnail(img);
    CHECK(svc.take_pending_thumbnail().getSize() == sf::Vector2u(8u, 8u));
    // Drained — the next take returns an empty image.
    CHECK(svc.take_pending_thumbnail().getSize() == sf::Vector2u(0u, 0u));
}
