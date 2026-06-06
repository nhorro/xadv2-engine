#include "engine/core/diagnostics.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/manifest.hpp"
#include "engine/core/save_service.hpp"
#include "engine/core/scene.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/state_store.hpp"
#include "notebook/notebook_model.hpp"
#include "notebook/notebook_module.hpp"
#include "notebook/notebook_scene.hpp"

#include <doctest/doctest.h>
#include <sol/sol.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

using namespace ingreso::notebook;

namespace {

const char* kNotebook = R"YAML(
version: 1
ui: { background: notebook.png, font: notebook.ttf }
evidence_sections:
  - { id: observations, label: Observaciones }
evidences:
  - { id: radial, section: observations, title: Radiales, note: Nota, tags: [fracture] }
  - { id: cuts, section: observations, title: Cortes, note: Nota, tags: [cuts] }
  - { id: context, section: observations, title: Contexto, note: Nota, tags: [context] }
hypotheses:
  - id: trauma
    title: Trauma
    template: argument
    conclusions:
      - { id: blunt, label: Contundente }
    premises:
      - { id: pattern, label: Patron, evidence_tags: [fracture] }
      - { id: context, label: Contexto, evidence_tags: [context] }
      - { id: context_optional, label: Contexto opcional, evidence_tags: [context], required: false }
  - id: classify
    title: Clasificar
    template: classification
    evidence_tags: [fracture]
    subjects:
      - id: one
        label: Uno
        evidence_tags: [fracture]
        options:
          - { id: yes, label: Si }
)YAML";

pac::core::Diagnostics quiet() {
    return pac::core::Diagnostics(pac::core::LogLevel::ERROR);
}

class TestScene : public pac::core::Scene {
public:
    void draw(sf::RenderTarget&) const override {}
};

struct NotebookTempDir {
    std::filesystem::path path;

    NotebookTempDir() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("pac_notebook_save_test_" + std::to_string(now));
        std::filesystem::create_directories(path);
    }
    ~NotebookTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace

TEST_CASE("notebook YAML validates ids and authored definitions") {
    const NotebookDefinitions defs = parse_notebook_yaml(kNotebook);
    CHECK(defs.evidences.size() == 3);
    CHECK(defs.hypotheses.size() == 2);
    CHECK(defs.ui.font_size == 19);
    CHECK(defs.ui.left_page.width == doctest::Approx(400.0f));
    CHECK(defs.ui.evidence_tab.label == "Evidencias");
    CHECK(defs.ui.close.action == NotebookCloseAction::Pop);
    CHECK(defs.ui.close.position.x == doctest::Approx(1120.0f));
    CHECK_THROWS(parse_notebook_yaml(R"YAML(
version: 1
ui: { background: b, font: f }
evidence_sections: [{ id: s, label: S }]
evidences:
  - { id: duplicate, section: s, title: A, note: N }
  - { id: duplicate, section: s, title: B, note: N }
hypotheses: []
)YAML"));
}

TEST_CASE("notebook YAML accepts configurable page and tab styling") {
    const NotebookDefinitions defs = parse_notebook_yaml(R"YAML(
version: 1
ui:
  background: b
  font: f
  font_size: 17
  text:
    outline_thickness: 0.75
    outline_color: [12, 13, 14, 15]
  pages:
    left: { x: 10, y: 20, width: 300, height: 400 }
    right: { x: 500, y: 30, width: 350, height: 390 }
  tabs:
    font_size: 28
    idle_color: [1, 2, 3]
    hover_color: [4, 5, 6, 7]
    selected_color: [8, 9, 10, 11]
    evidence:
      label: E
      position: { x: 12, y: 13 }
      hitbox: { x: 11, y: 12, width: 80, height: 30 }
    hypotheses:
      label: H
      position: { x: 92, y: 13 }
      hitbox: { x: 91, y: 12, width: 90, height: 30 }
  close:
    label: Cerrar
    position: { x: 850, y: 20 }
    hitbox: { x: 840, y: 10, width: 50, height: 35 }
    font_size: 21
    action: goto
    scene: title
evidence_sections: []
evidences: []
hypotheses: []
)YAML");
    CHECK(defs.ui.font_size == 17);
    CHECK(defs.ui.text.outline_thickness == doctest::Approx(0.75f));
    CHECK(defs.ui.text.outline_color.r == 12);
    CHECK(defs.ui.text.outline_color.a == 15);
    CHECK(defs.ui.left_page.x == doctest::Approx(10.0f));
    CHECK(defs.ui.right_page.width == doctest::Approx(350.0f));
    CHECK(defs.ui.tab_font_size == 28);
    CHECK(defs.ui.tab_idle.a == 255);
    CHECK(defs.ui.tab_hover.a == 7);
    CHECK(defs.ui.tab_selected.r == 8);
    CHECK(defs.ui.evidence_tab.label == "E");
    CHECK(defs.ui.hypotheses_tab.hitbox.width == doctest::Approx(90.0f));
    CHECK(defs.ui.close.label == "Cerrar");
    CHECK(defs.ui.close.font_size == 21);
    CHECK(defs.ui.close.action == NotebookCloseAction::Goto);
    CHECK(defs.ui.close.scene == "title");
}

TEST_CASE("notebook YAML rejects unsupported close actions") {
    CHECK_THROWS(parse_notebook_yaml(R"YAML(
version: 1
ui:
  background: b
  font: f
  close: { action: quit }
evidence_sections: []
evidences: []
hypotheses: []
)YAML"));
    CHECK_THROWS(parse_notebook_yaml(R"YAML(
version: 1
ui:
  background: b
  font: f
  close: { action: goto }
evidence_sections: []
evidences: []
hypotheses: []
)YAML"));
}

TEST_CASE("notebook YAML rejects negative text outline thickness") {
    CHECK_THROWS(parse_notebook_yaml(R"YAML(
version: 1
ui:
  background: b
  font: f
  text: { outline_thickness: -1 }
evidence_sections: []
evidences: []
hypotheses: []
)YAML"));
}

TEST_CASE("authored ingreso urgente notebook YAML parses") {
    std::ifstream in(std::string(PAC_SOURCE_DIR) +
                     "/games/ingreso_urgente/data/notebook/notebook.yaml");
    REQUIRE(in.good());
    std::ostringstream text;
    text << in.rdbuf();
    const NotebookDefinitions defs = parse_notebook_yaml(text.str());
    CHECK(defs.evidences.size() == 44);
    CHECK(defs.hypotheses.size() == 2);
    CHECK(defs.ui.font_size > 0);
    CHECK_FALSE(defs.ui.hypotheses_tab.label.empty());
    CHECK(defs.ui.close.font_size > 0);

    pac::core::Diagnostics log = quiet();
    pac::core::StateStore state;
    NotebookModel model(defs, state, log);
    CHECK(model.hasEvidence("dummy_layout_01"));
    CHECK(model.hasClassificationCandidates("camelid_classification", "camelid1"));
    CHECK(model.cycleClassificationSupport("camelid_classification", "camelid1", 0) ==
          "dummy_support_camelid1");
    CHECK(model.hasArgumentCandidates("matilde_trauma", "fracture_pattern"));
    CHECK(model.cycleArgumentEvidence("matilde_trauma", "fracture_pattern") ==
          "dummy_support_fracture_pattern");
    CHECK(model.cycleArgumentEvidence("matilde_trauma", "no_cut_marks") ==
          "dummy_support_no_cut_marks");
    CHECK(model.cycleArgumentEvidence("matilde_trauma", "no_collapse") ==
          "dummy_support_no_collapse");
    CHECK(model.cycleArgumentEvidence("matilde_trauma", "perimortem") ==
          "dummy_support_perimortem");
    state.clear(); // TitleScreen new-game reset.
    CHECK(model.hasEvidence("dummy_layout_01"));
}

TEST_CASE("authored ingreso urgente manifest declares notebook definitions path") {
    const pac::core::Manifest manifest = pac::core::load_manifest(
        std::string(PAC_SOURCE_DIR) + "/games/ingreso_urgente/data/game.yaml");
    const pac::core::SceneDesc* notebook = manifest.find_scene("notebook");
    REQUIRE(notebook != nullptr);
    CHECK(notebook->type == "IngresoNotebook");
    CHECK(notebook->parameters.get_or("data", "") == "notebook/notebook.yaml");
}

TEST_CASE("notebook close routing defaults to pop and supports declarative goto") {
    pac::core::SceneManager scenes;
    scenes.set_builder([](const std::string&) { return std::make_unique<TestScene>(); });
    scenes.goto_scene("room");
    scenes.apply_pending();
    scenes.push_scene("notebook");
    scenes.apply_pending();
    REQUIRE(scenes.size() == 2);

    routeNotebookClose(NotebookUiClose{}, scenes);
    scenes.apply_pending();
    CHECK(scenes.size() == 1);
    CHECK(scenes.current_scene_id() == "room");

    NotebookUiClose close;
    close.action = NotebookCloseAction::Goto;
    close.scene = "title";
    routeNotebookClose(close, scenes);
    scenes.apply_pending();
    CHECK(scenes.size() == 1);
    CHECK(scenes.current_scene_id() == "title");
}

TEST_CASE("discovery filtering cycling completeness and stale selections are safe") {
    pac::core::Diagnostics log = quiet();
    pac::core::StateStore state;
    NotebookModel model(parse_notebook_yaml(kNotebook), state, log);

    CHECK(model.discoveredEvidenceCount() == 0);
    CHECK_FALSE(model.hasArgumentCandidates("trauma", "pattern"));
    CHECK(model.discoverEvidence("radial"));
    CHECK(model.discoverEvidence("context"));
    CHECK(model.hasArgumentCandidates("trauma", "pattern"));
    CHECK(model.discoveredEvidenceCount() == 2);
    CHECK(model.discoveredEvidenceCountByTag("fracture") == 1);

    model.setConclusion("trauma", "blunt");
    CHECK(model.cycleArgumentEvidence("trauma", "pattern") == "radial");
    CHECK(model.cycleArgumentEvidence("trauma", "context") == "context");
    CHECK(model.cycleArgumentEvidence("trauma", "context_optional").empty());
    CHECK(model.isHypothesisComplete("trauma"));

    state.set("notebook.hypothesis.trauma.premise.context.evidence", std::string("radial"));
    CHECK_FALSE(model.isHypothesisComplete("trauma"));
    state.set("notebook.hypothesis.trauma.premise.context.evidence", std::string("stale"));
    CHECK(model.hypothesisEvidence("trauma", "context").empty());
    CHECK_FALSE(model.isHypothesisComplete("trauma"));
}

TEST_CASE("classification state cycles without reusing support evidence") {
    pac::core::Diagnostics log = quiet();
    pac::core::StateStore state;
    NotebookModel model(parse_notebook_yaml(kNotebook), state, log);
    CHECK_FALSE(model.hasClassificationCandidates("classify", "one"));
    model.discoverEvidence("radial");
    CHECK(model.hasClassificationCandidates("classify", "one"));
    model.setClassification("classify", "one", "yes");
    CHECK(model.cycleClassificationSupport("classify", "one", 0) == "radial");
    CHECK(model.hasClassification("classify", "one", "yes"));
    CHECK(model.hasClassificationSupport("classify", "one", "radial"));
    CHECK(model.isHypothesisComplete("classify"));
    CHECK(model.cycleClassificationSupport("classify", "one", 0).empty());
}

TEST_CASE("notebook scalar state round-trips through engine save service") {
    pac::core::Diagnostics log = quiet();
    NotebookTempDir td;
    pac::core::SaveService saves(td.path, log);
    const NotebookDefinitions defs = parse_notebook_yaml(kNotebook);

    pac::core::StateStore state;
    NotebookModel model(defs, state, log);
    REQUIRE(model.discoverEvidence("radial"));
    REQUIRE(model.discoverEvidence("context"));
    model.setConclusion("trauma", "blunt");
    CHECK(model.cycleArgumentEvidence("trauma", "pattern") == "radial");
    CHECK(model.cycleArgumentEvidence("trauma", "context") == "context");
    model.setClassification("classify", "one", "yes");
    CHECK(model.cycleClassificationSupport("classify", "one", 0) == "radial");

    pac::core::GameState snapshot;
    snapshot.current_scene_id = "room_view";
    snapshot.room_view.current_room_id = "lab";
    snapshot.global_state = state.entries();
    REQUIRE(saves.save(1, snapshot));

    const auto loaded = saves.load(1);
    REQUIRE(loaded.has_value());
    pac::core::StateStore restored_state;
    restored_state.replace_all(loaded->global_state);
    NotebookModel restored(defs, restored_state, log);

    CHECK(restored.hasEvidence("radial"));
    CHECK(restored.hasEvidence("context"));
    CHECK(restored.hypothesisConclusion("trauma") == "blunt");
    CHECK(restored.hypothesisEvidence("trauma", "pattern") == "radial");
    CHECK(restored.hypothesisEvidence("trauma", "context") == "context");
    CHECK(restored.hasClassification("classify", "one", "yes"));
    CHECK(restored.classificationSupport("classify", "one", 0) == "radial");
}

TEST_CASE("flat Lua globals and Notebook aliases bind game-local API") {
    pac::core::Diagnostics log = quiet();
    pac::core::StateStore state;
    pac::core::Scripting scripting(log);
    pac::core::SceneManager scenes;
    NotebookModel model(parse_notebook_yaml(kNotebook), state, log);
    bindNotebookLua(scripting, model, scenes, "notebook");
    CHECK(scripting.run_string(R"LUA(
      assert(discover_evidence("radial"))
      assert(has_evidence("radial"))
      assert(Notebook.hasEvidence("radial"))
      assert(get_discovered_evidence_count() == 1)
      assert(Notebook.getDiscoveredEvidenceCountByTag("fracture") == 1)
      Notebook.open()
    )LUA"));
}

TEST_CASE("headless notebook UI controller changes tabs sections hypotheses and close state") {
    pac::core::Diagnostics log = quiet();
    pac::core::StateStore state;
    NotebookModel model(parse_notebook_yaml(kNotebook), state, log);
    NotebookUiController ui(model);
    CHECK(ui.tab() == NotebookUiController::Tab::Evidence);
    ui.setTab(NotebookUiController::Tab::Hypotheses);
    CHECK(ui.tab() == NotebookUiController::Tab::Hypotheses);
    CHECK(ui.sectionOpen("observations"));
    ui.toggleSection("observations");
    CHECK_FALSE(ui.sectionOpen("observations"));
    ui.nextHypothesis(model, 1);
    CHECK(ui.hypothesisIndex() == 1);
    ui.toggleClassificationSelector("one");
    CHECK(ui.classificationSelectorOpen("one"));
    ui.toggleClassificationSelector("two");
    CHECK_FALSE(ui.classificationSelectorOpen("one"));
    CHECK(ui.classificationSelectorOpen("two"));
    ui.nextHypothesis(model, 1);
    CHECK_FALSE(ui.classificationSelectorOpen("two"));
    ui.requestClose();
    CHECK(ui.closeRequested());
}

TEST_CASE("notebook evidence layout wraps titles and clamps scrollbar positions") {
    const auto lines =
        wrapNotebookWords("Titulo deliberadamente largo para pasar a la siguiente linea", 22);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "Titulo deliberadamente");
    CHECK(lines[1] == "largo para pasar a la");
    CHECK(lines[2] == "siguiente linea");

    NotebookScrollMetrics metrics{900.0f, 450.0f};
    CHECK(metrics.maxScroll() == doctest::Approx(450.0f));
    CHECK(metrics.clamp(-10.0f) == doctest::Approx(0.0f));
    CHECK(metrics.clamp(800.0f) == doctest::Approx(450.0f));
    CHECK(metrics.thumbHeight(450.0f) == doctest::Approx(225.0f));
    CHECK(metrics.thumbTop(225.0f, 175.0f, 450.0f) == doctest::Approx(287.5f));
    CHECK(metrics.scrollFromThumbTop(287.5f, 175.0f, 450.0f) == doctest::Approx(225.0f));
}

TEST_CASE("notebook combo labels truncate with ellipsis without splitting UTF-8") {
    CHECK(truncateNotebookText(nullptr, "abcdefghi", 10, 34.0f) == "abc...");
    CHECK(truncateNotebookText(nullptr, "ábcdefghi", 10, 34.0f) == "ábc...");
    CHECK(truncateNotebookText(nullptr, "corto", 10, 34.0f) == "corto");
}

TEST_CASE("act I notebook integration scripts compile in embedded Lua") {
    pac::core::Diagnostics log = quiet();
    pac::core::Scripting scripting(log);
    const char* scripts[] = {
        "games/ingreso_urgente/data/closeups/lab_window_llamas/logic.lua",
        "games/ingreso_urgente/data/closeups/lab_skull/logic.lua",
        "games/ingreso_urgente/data/closeups/lab_chalkboard/logic.lua",
        "games/ingreso_urgente/data/closeups/lab_notebook1/logic.lua",
        "games/ingreso_urgente/data/closeups/lab_notebook2/logic.lua",
        "games/ingreso_urgente/data/dialogs/skull_trauma_cause.lua",
    };
    for (const char* logical : scripts) {
        const std::string path = std::string(PAC_SOURCE_DIR) + "/" + logical;
        INFO(path);
        CHECK(scripting.lua().load_file(path).valid());
    }
}

TEST_CASE("room scripts and per-config beat modules compile in embedded Lua") {
    pac::core::Diagnostics log = quiet();
    pac::core::Scripting scripting(log);
    // Per #185 the per-config presence files + the _room_flow helper are gone
    // (presence is declarative in each room's YAML `configs:`). Only the room
    // scripts and the surviving per-config BEAT module (the lab intro) remain.
    const char* scripts[] = {
        "games/ingreso_urgente/data/rooms/lab.lua",
        "games/ingreso_urgente/data/rooms/lab/act1/intro.lua",
        "games/ingreso_urgente/data/rooms/hall.lua",
        "games/ingreso_urgente/data/rooms/exterior.lua",
    };
    for (const char* logical : scripts) {
        const std::string path = std::string(PAC_SOURCE_DIR) + "/" + logical;
        INFO(path);
        CHECK(scripting.lua().load_file(path).valid());
    }
}
