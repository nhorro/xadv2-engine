#include "engine/pnc/dev_actions.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace pac::pnc;

TEST_CASE("room_ids_in_dir lists *.yaml stems, sorted, ignoring other files") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "pac_dev_actions_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto touch = [&](const std::string& name) { std::ofstream(dir / name) << "x"; };
    touch("study.yaml");
    touch("hall.yaml");
    touch("exterior.yaml");
    touch("study.lua"); // a room's behavior, not a room yaml
    touch("notes.txt"); // unrelated

    CHECK(room_ids_in_dir(dir.string()) == std::vector<std::string>{"exterior", "hall", "study"});

    fs::remove_all(dir);
}

TEST_CASE("room_ids_in_dir returns empty for a missing directory") {
    const std::string missing =
        (std::filesystem::temp_directory_path() / "pac_no_such_dir_xyzzy").string();
    CHECK(room_ids_in_dir(missing).empty());
}
