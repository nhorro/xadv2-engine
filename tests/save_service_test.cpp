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
