#include "engine/core/resource_source.hpp"
#include "engine/pnc/dev_actions.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace pac::pnc;
using pac::core::FilesystemResourceSource;

TEST_CASE("room_ids_in_dir lists *.yaml stems, sorted, ignoring other files") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "pac_dev_actions_test";
    const fs::path rooms = root / "rooms";
    fs::remove_all(root);
    fs::create_directories(rooms);
    const auto touch = [&](const std::string& rel) { std::ofstream(rooms / rel) << "x"; };
    touch("study.yaml");
    touch("hall.yaml");
    touch("exterior.yaml");
    touch("study.lua"); // a room's behavior, not a room yaml
    touch("notes.txt"); // unrelated

    FilesystemResourceSource source(root.string());
    CHECK(room_ids_in_dir(source, "rooms") ==
          std::vector<std::string>{"exterior", "hall", "study"});

    fs::remove_all(root);
}

TEST_CASE("room_ids_in_dir returns empty for a missing directory") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "pac_no_such_dir_xyzzy";
    fs::remove_all(root);

    FilesystemResourceSource source(root.string());
    CHECK(room_ids_in_dir(source, "rooms").empty());
}
