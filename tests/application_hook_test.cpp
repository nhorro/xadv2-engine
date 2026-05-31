#include "engine/core/application.hpp"

#include "engine/core/engine_context.hpp"
#include "engine/core/manifest.hpp"
#include "engine/core/scene_factory.hpp"
#include "engine/core/scripting.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <string>

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

} // namespace

TEST_CASE("application configure hook runs after core Lua bindings and setup failure aborts startup") {
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
               "resources: { src: \"" << td.path.string() << "\" }\n"
               "strings: strings.yaml\n"
               "entry: blank\n"
               "scenes: [{ id: blank, type: Blank }]\n";
    }

    bool called = false;
    pac::core::ApplicationHooks hooks;
    hooks.configure = [&called](pac::core::EngineContext& ctx, const pac::core::Manifest& manifest) {
        called = true;
        CHECK(manifest.id == "application_hook_test");
        CHECK(ctx.scripting.run_string("assert(type(get_state) == 'function')"));
        throw std::runtime_error("expected setup failure");
    };

    pac::core::SceneFactory factory;
    CHECK(pac::core::run(manifest.string(), factory, {}, hooks) == 1);
    CHECK(called);
}
