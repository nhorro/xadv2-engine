#include "engine/core/gameplay_recorder.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using pac::core::GameplayEvent;
using pac::core::GameplayRecorder;

namespace {

struct RecordingFile {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "pac_gameplay_recorder_test.csv";
    RecordingFile() { std::filesystem::remove(path); }
    ~RecordingFile() { std::filesystem::remove(path); }
};

} // namespace

TEST_CASE("gameplay recorder writes the documented CSV schema and publishes semantic events") {
    RecordingFile file;
    GameplayRecorder recorder;
    std::vector<GameplayEvent> observed;
    recorder.add_observer([&observed](const GameplayEvent& event) { observed.push_back(event); });
    REQUIRE(recorder.start_csv(file.path));

    const std::string data = GameplayRecorder::json_object(
        {{"speaker", "ana"}, {"text", "A line; with \"quotes\"\nand a newline"}});
    recorder.record("speech", "dialog.ana.greeting", data);

    REQUIRE(observed.size() == 1);
    CHECK(observed[0].timestamp >= 0.0);
    CHECK(observed[0].event_type == "speech");
    CHECK(observed[0].event_id == "dialog.ana.greeting");
    CHECK(observed[0].event_data == data);

    std::ifstream in(file.path);
    const std::string csv((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(csv.starts_with("timestamp;event_type;event_id;event_data\n"));
    CHECK(csv.find(";speech;dialog.ana.greeting;") != std::string::npos);
    // event_data contains JSON quotes, a semicolon, and an escaped newline, so
    // the CSV cell is quoted and each literal quote is doubled.
    CHECK(csv.find("\"{\"\"speaker\"\":\"\"ana\"\"") != std::string::npos);
    CHECK(csv.find("quotes") != std::string::npos);
    CHECK(csv.find("\\nand a newline") != std::string::npos);
}

TEST_CASE("gameplay recorder can stream events without enabling CSV") {
    GameplayRecorder recorder;
    int calls = 0;
    recorder.add_observer([&calls](const GameplayEvent&) { ++calls; });
    recorder.record("scene_enter", "title");
    CHECK(calls == 1);
}
