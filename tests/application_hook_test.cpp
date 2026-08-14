#include "engine/core/application.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/manifest.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/scene_factory.hpp"
#include "engine/core/scripting.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct TempDir {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "pac_application_hook_test";
    TempDir() {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

class MapSource : public pac::core::ResourceSource {
public:
    std::map<std::string, std::string> files;

    bool exists(const std::string& logical) const override { return files.contains(logical); }
    std::string read_text(const std::string& logical) const override {
        const auto it = files.find(logical);
        if (it == files.end()) {
            throw pac::core::ResourceError("missing: " + logical);
        }
        return it->second;
    }
    std::vector<std::byte> read_bytes(const std::string&) const override { return {}; }
};

} // namespace

TEST_CASE(
    "application configure hook runs after core Lua bindings and setup failure aborts startup") {
#if defined(_WIN32)
    _putenv_s("ALSOFT_DRIVERS", "null");
#else
    setenv("ALSOFT_DRIVERS", "null", 1);
#endif
    TempDir td;
    const auto strings = td.path / "strings.yaml";
    const auto manifest = td.path / "game.yaml";
    {
        std::ofstream out(strings);
        out << "version: 1\n"
               "language: es\n"
               "verbs: { look_at: Look }\n"
               "connectors: { use: with }\n"
               "ui: { pause: Pause }\n"
               "defaults:\n"
               "  cant_look_at: x\n"
               "  cant_pick_up: x\n"
               "  wont_open: x\n"
               "  wont_close: x\n"
               "  wont_push: x\n"
               "  wont_pull: x\n"
               "  cant_use_that_way: x\n"
               "  no_one_to_give_to: x\n"
               "  nothing_to_say: x\n"
               "  nothing_happens: x\n";
    }
    {
        std::ofstream out(manifest);
        out << "version: 1\n"
               "id: application_hook_test\n"
               "resolution: { width: 320, height: 200 }\n"
               "window: { fullscreen: false, width: 320, height: 200 }\n"
               // generic_string(), not string(): a Windows temp path
               // (C:\Users\RUNNER~1\...) inside a YAML double-quoted scalar makes
               // `\U` a Unicode escape, and the loader dies with "bad character
               // found while scanning hex number". Forward slashes are valid on
               // Windows and mean nothing special to YAML.
               "resources: { src: \""
            << td.path.generic_string()
            << "\" }\n"
               "strings: strings.yaml\n"
               "entry: blank\n"
               "scenes: [{ id: blank, type: Blank }]\n";
    }

    bool called = false;
    pac::core::ApplicationHooks hooks;
    hooks.configure = [&called](pac::core::EngineContext& ctx,
                                const pac::core::Manifest& manifest) {
        called = true;
        CHECK(manifest.id == "application_hook_test");
        CHECK(ctx.scripting.run_string("assert(type(get_state) == 'function')"));
        CHECK(ctx.scripting.run_string("assert(type(stop_sound) == 'function')"));
        throw std::runtime_error("expected setup failure");
    };

    pac::core::SceneFactory factory;
    CHECK(pac::core::run(manifest.string(), factory, {}, hooks) == 1);
    CHECK(called);
}

TEST_CASE("application can bootstrap its manifest and assets from one ResourceSource") {
#if defined(_WIN32)
    _putenv_s("ALSOFT_DRIVERS", "null");
#else
    setenv("ALSOFT_DRIVERS", "null", 1);
#endif
    MapSource source;
    source.files["game.yaml"] =
        "version: 1\n"
        "id: application_resource_source_test\n"
        "resolution: { width: 320, height: 200 }\n"
        "window: { fullscreen: false, width: 320, height: 200 }\n"
        "resources: { src: ignored-by-supplied-source }\n"
        "strings: strings.yaml\n"
        "entry: blank\n"
        "scenes: [{ id: blank, type: Blank }]\n";
    source.files["strings.yaml"] =
        "version: 1\n"
        "language: es\n"
        "verbs: { look_at: Look }\n"
        "connectors: { use: with }\n"
        "ui: { pause: Pause }\n"
        "defaults:\n"
        "  cant_look_at: x\n"
        "  cant_pick_up: x\n"
        "  wont_open: x\n"
        "  wont_close: x\n"
        "  wont_push: x\n"
        "  wont_pull: x\n"
        "  cant_use_that_way: x\n"
        "  no_one_to_give_to: x\n"
        "  nothing_to_say: x\n"
        "  nothing_happens: x\n";

    bool called = false;
    pac::core::ApplicationHooks hooks;
    hooks.configure = [&called](pac::core::EngineContext&, const pac::core::Manifest& manifest) {
        called = true;
        CHECK(manifest.id == "application_resource_source_test");
        CHECK(manifest.resources_src.empty());
        throw std::runtime_error("expected setup failure");
    };

    pac::core::SceneFactory factory;
    CHECK(pac::core::run_from_resources(source, "game.yaml", factory, {}, hooks) == 1);
    CHECK(called);
}
