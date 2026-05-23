#include "engine/core/scene.hpp"
#include "engine/core/scene_manager.hpp"

#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

using namespace pac::core;

namespace {

struct Counts {
    int entered = 0;
    int left = 0;
};

std::map<std::string, Counts> g_counts;

struct DummyScene : Scene {
    std::string id;
    explicit DummyScene(std::string scene_id) : id(std::move(scene_id)) {}
    void enter() override { g_counts[id].entered++; }
    void leave() override { g_counts[id].left++; }
    void draw(sf::RenderTarget&) const override {}
};

SceneManager make_manager() {
    SceneManager m;
    m.set_builder([](const std::string& id) -> std::unique_ptr<Scene> {
        if (id == "bad") {
            return nullptr; // simulate unknown scene
        }
        return std::make_unique<DummyScene>(id);
    });
    return m;
}

} // namespace

TEST_CASE("goto / push / pop lifecycle and stack size") {
    g_counts.clear();
    SceneManager m = make_manager();
    CHECK(m.size() == 0);
    CHECK(m.running());

    m.goto_scene("a");
    m.apply_pending();
    CHECK(m.size() == 1);
    CHECK(g_counts["a"].entered == 1);

    m.push_scene("b");
    m.apply_pending();
    CHECK(m.size() == 2);
    CHECK(g_counts["b"].entered == 1);

    m.pop_scene();
    m.apply_pending();
    CHECK(m.size() == 1);
    CHECK(g_counts["b"].left == 1);

    m.goto_scene("c"); // replaces "a"
    m.apply_pending();
    CHECK(m.size() == 1);
    CHECK(g_counts["a"].left == 1);
    CHECK(g_counts["c"].entered == 1);
}

TEST_CASE("transitions are deferred until apply_pending") {
    SceneManager m = make_manager();
    m.goto_scene("a");
    CHECK(m.size() == 0); // not applied yet
    m.apply_pending();
    CHECK(m.size() == 1);
}

TEST_CASE("quit empties the stack and stops running") {
    g_counts.clear();
    SceneManager m = make_manager();
    m.goto_scene("a");
    m.push_scene("b");
    m.apply_pending();
    m.quit();
    m.apply_pending();
    CHECK_FALSE(m.running());
    CHECK(m.size() == 0);
    CHECK(g_counts["a"].left == 1);
    CHECK(g_counts["b"].left == 1);
}

TEST_CASE("QUIT token routes to quit") {
    SceneManager m = make_manager();
    m.goto_scene("a");
    m.apply_pending();
    m.goto_scene("QUIT");
    m.apply_pending();
    CHECK_FALSE(m.running());
}

TEST_CASE("unknown scene id stops the manager") {
    SceneManager m = make_manager();
    m.goto_scene("bad");
    m.apply_pending();
    CHECK_FALSE(m.running());
    CHECK(m.size() == 0);
}

TEST_CASE("popping the last scene stops the manager") {
    SceneManager m = make_manager();
    m.goto_scene("a");
    m.apply_pending();
    m.pop_scene();
    m.apply_pending();
    CHECK(m.size() == 0);
    CHECK_FALSE(m.running());
}

TEST_CASE("a faded goto holds the swap until black, then swaps and fades in") {
    g_counts.clear();
    SceneManager m = make_manager();
    m.goto_scene("a");
    m.apply_pending(); // entry scene is instant (no duration set yet)
    REQUIRE(m.size() == 1);

    m.set_transition_duration(1.0f);
    m.goto_scene("b");
    m.apply_pending();
    // Fading out: "b" not built yet, "a" still on top.
    CHECK(m.transitioning());
    CHECK(g_counts["b"].entered == 0);
    CHECK(g_counts["a"].left == 0);

    m.update(0.5f); // halfway through the fade-out
    m.apply_pending();
    CHECK(m.transitioning());
    CHECK(g_counts["b"].entered == 0);

    m.update(0.5f); // reaches black -> swap + fade-in begins
    m.apply_pending();
    CHECK_FALSE(m.transitioning());
    CHECK(g_counts["a"].left == 1);
    CHECK(g_counts["b"].entered == 1);
    CHECK(m.size() == 1);
}

TEST_CASE("overlays (push/pop) are never faded") {
    g_counts.clear();
    SceneManager m = make_manager();
    m.goto_scene("a");
    m.apply_pending();
    m.set_transition_duration(1.0f);

    m.push_scene("b"); // overlay (e.g. pause/settings) applies instantly
    m.apply_pending();
    CHECK_FALSE(m.transitioning());
    CHECK(m.size() == 2);
    CHECK(g_counts["b"].entered == 1);

    m.pop_scene();
    m.apply_pending();
    CHECK_FALSE(m.transitioning());
    CHECK(m.size() == 1);
}
