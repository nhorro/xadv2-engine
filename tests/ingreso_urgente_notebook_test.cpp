#include "engine/core/diagnostics.hpp"
#include "engine/core/facts.hpp"
#include "engine/core/game_state.hpp"
#include "engine/core/lua_api.hpp"
#include "engine/core/manifest.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/save_service.hpp"
#include "engine/core/scene.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/state_store.hpp"
#include "engine/pnc/closeup_runtime.hpp"
#include "engine/pnc/dialog.hpp"
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
#include <variant>
#include <vector>

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
    // Redesigned Act-1 notebook (02_act1.md §A.7 / Apéndice C): six evidences, each
    // with a single canonical source, and three hypotheses — the bone-individualization
    // tutorial, the matilde_trauma argument, and the Act-2 box_label classification.
    CHECK(defs.evidences.size() == 6);
    CHECK(defs.hypotheses.size() == 3);
    CHECK(defs.ui.font_size > 0);
    CHECK_FALSE(defs.ui.hypotheses_tab.label.empty());
    CHECK(defs.ui.close.font_size > 0);

    pac::core::Diagnostics log = quiet();
    pac::core::StateStore state;
    NotebookModel model(defs, state, log);

    // CONCERN 1: every finding id the close-ups pass to discover_evidence resolves to
    // a declared notebook evidence and registers when observed.
    const std::vector<std::string> findings = {"radial_fractures", "no_cut_marks",
                                               "no_collapse",      "primary_burial",
                                               "heavy_lithic",     "perimortem_possible"};
    for (const std::string& id : findings) {
        REQUIRE(model.evidence(id) != nullptr); // declared in notebook.yaml
        CHECK_FALSE(model.hasEvidence(id));      // not in the notebook until observed
        CHECK(model.discoverEvidence(id));
        CHECK(model.hasEvidence(id));
    }
    CHECK(model.discoveredEvidenceCount() == 6);

    // CONCERN 2 (notebook side): matilde_trauma stays INCOMPLETE until the conclusion
    // and every required premise are filled — exactly what the Schneider defence reads
    // through is_hypothesis_complete + get_hypothesis_conclusion.
    CHECK_FALSE(model.isHypothesisComplete("matilde_trauma"));
    model.setConclusion("matilde_trauma", "blunt_perimortem");
    CHECK(model.hypothesisConclusion("matilde_trauma") == "blunt_perimortem");
    CHECK_FALSE(model.isHypothesisComplete("matilde_trauma")); // premises still empty

    // Each required premise has exactly one matching discovered evidence (tags map
    // 1:1, §A.7), so cycling once selects it.
    CHECK(model.cycleArgumentEvidence("matilde_trauma", "fracture_pattern") == "radial_fractures");
    CHECK(model.cycleArgumentEvidence("matilde_trauma", "no_cut_marks") == "no_cut_marks");
    CHECK(model.cycleArgumentEvidence("matilde_trauma", "perimortem") == "perimortem_possible");
    CHECK(model.cycleArgumentEvidence("matilde_trauma", "primary_burial") == "primary_burial");
    CHECK_FALSE(model.isHypothesisComplete("matilde_trauma")); // no_collapse still missing
    CHECK(model.cycleArgumentEvidence("matilde_trauma", "no_collapse") == "no_collapse");

    // Conclusion + all required premises filled -> complete (artifact is optional).
    CHECK(model.isHypothesisComplete("matilde_trauma"));
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

// =====================================================================
//  Schneider puzzle dialog — content walkthrough (#187)
// =====================================================================
//
// The puzzle dialog (dialogs/skull_trauma_cause.lua) was rewritten with the
// `dialog { ... }` / `topic` shorthand. `load_file` above only *compiles* it;
// it never runs `dialog{}`, so the expansion + validator are exercised here by
// driving the real file through `DialogRuntime::start` and walking the full
// winning path. A headless stand-in for the player clicking through the scene.
namespace {

// A no-render dialog host: it records spoken lines and lets the test clear the
// bubble so the runtime advances. Mirrors the engine-side host minimally.
struct DialogTestHost {
    std::vector<std::string> npc;
    std::vector<std::string> player;
    bool speaking = false;
    std::set<std::pair<std::string, int>> consumed;

    pac::pnc::DialogHost host() {
        pac::pnc::DialogHost h;
        h.speak_npc = [this](const std::string& t) {
            npc.push_back(t);
            speaking = true;
        };
        h.speak_player = [this](const std::string& t) {
            player.push_back(t);
            speaking = true;
        };
        h.is_speaking = [this]() { return speaking; };
        h.is_option_consumed = [this](const std::string& node, int idx) {
            return consumed.count({node, idx}) > 0;
        };
        h.mark_option_consumed = [this](const std::string& node, int idx) {
            consumed.insert({node, idx});
        };
        return h;
    }
};

void bind_dialog_state(pac::core::Scripting& s, pac::core::StateStore& store) {
    sol::state& L = s.lua();
    L.set_function("get_state", [&store, &L](const std::string& key) -> sol::object {
        const auto v = store.get(key);
        if (!v) {
            return sol::make_object(L, sol::lua_nil);
        }
        return std::visit([&L](const auto& x) { return sol::make_object(L, x); }, *v);
    });
    L.set_function("set_state", [&store](const std::string& key, sol::object value) {
        if (value.is<bool>()) {
            store.set(key, value.as<bool>());
        } else if (value.is<double>()) {
            store.set(key, value.as<double>());
        } else if (value.is<std::string>()) {
            store.set(key, value.as<std::string>());
        }
    });
}

void advance_to_choice(pac::pnc::DialogRuntime& d, DialogTestHost& host) {
    for (int i = 0; i < 80; ++i) {
        if (d.state() == pac::pnc::DialogRuntime::State::AWAITING_CHOICE || d.ended()) {
            return;
        }
        host.speaking = false;
        d.update();
    }
    FAIL("dialog did not settle on a choice");
}

bool has_option(const pac::pnc::DialogRuntime& d, const std::string& text) {
    for (const pac::pnc::DialogOption& o : d.options()) {
        if (o.text == text) {
            return true;
        }
    }
    return false;
}

void choose_text(pac::pnc::DialogRuntime& d, const std::string& text) {
    for (const pac::pnc::DialogOption& o : d.options()) {
        if (o.text == text) {
            d.choose(o.index);
            return;
        }
    }
    FAIL("no visible option with text: " << text);
}

// State a claim from the hub and return to the hub.
void state_claim(pac::pnc::DialogRuntime& d, DialogTestHost& host, const std::string& line) {
    REQUIRE(has_option(d, line));
    choose_text(d, line);
    advance_to_choice(d, host);
}

} // namespace

TEST_CASE("Schneider puzzle dialog expands and walks to the winning conclusion") {
    pac::core::Diagnostics log = quiet();
    pac::core::Scripting s(log);
    pac::core::StateStore state;
    bind_dialog_state(s, state);

    // The notebook predicates the dialog consults to gate the final hypothesis.
    s.lua().set_function("get_hypothesis_conclusion", [](const std::string& id) -> std::string {
        return id == "matilde_trauma" ? "blunt_perimortem" : "";
    });
    s.lua().set_function("is_hypothesis_complete",
                         [](const std::string& id) { return id == "matilde_trauma"; });

    // Findings the player has already gathered in the room/close-ups: these are
    // the `requires` keys that make the observation topics appear.
    state.set("finding.radial_fractures", true);
    state.set("finding.no_cut_marks", true);
    state.set("finding.no_collapse", true);
    state.set("finding.perimortem_possible", true);

    pac::core::FilesystemResourceSource source(std::string(PAC_SOURCE_DIR) +
                                               "/games/ingreso_urgente/data");
    pac::core::ResourceCache resources(source, log);

    // The dialog records the case state through `facts.case.*`; bind the proxy
    // over the game's real facts.yaml, in dev mode, and assert the dialog touches
    // only declared facts (no typo-guard warnings).
    std::vector<std::string> fact_warnings;
    const pac::core::FactsRegistry facts =
        pac::core::FactsRegistry::parse(resources.read_text("facts.yaml"));
    pac::core::bind_facts(s, facts, /*dev_warn=*/true, [&](const std::string& m) {
        fact_warnings.push_back(m);
    });

    DialogTestHost host;
    auto dopt =
        pac::pnc::DialogRuntime::start(s, resources, log, "skull_trauma_cause", host.host());
    // start() ran dialog{} expansion AND the validator on the result: a non-null
    // runtime proves the rewritten tree is well-formed (no dangling `to`, no id
    // collision between a topic and a raw node, START present).
    REQUIRE(dopt.has_value());
    pac::pnc::DialogRuntime& d = *dopt;

    advance_to_choice(d, host); // past the multi-line intro into the hub
    REQUIRE(d.current_node() == "hub");

    // Traps are gated until the basic observations are on the table.
    CHECK_FALSE(has_option(d, "Entonces fue asesinado."));
    // `after`-gated discards are not yet offered.
    CHECK_FALSE(has_option(d, "Una herramienta filosa no explica bien el patrón."));

    // State the three basic observations (each a generated topic option).
    state_claim(d, host, "El cráneo muestra una lesión focal con fracturas radiales.");
    state_claim(d, host, "No se ven marcas de corte en la superficie ósea.");
    state_claim(d,
                host,
                "El registro contextual no muestra derrumbe ni compresión sedimentaria clara.");

    // Stated topics do not reappear; the murder trap is now available.
    CHECK_FALSE(has_option(d, "No se ven marcas de corte en la superficie ósea."));
    CHECK(has_option(d, "Entonces fue asesinado."));

    // The discards unlocked via `after =` their observations.
    CHECK(has_option(d, "Una herramienta filosa no explica bien el patrón."));
    state_claim(d, host, "Una herramienta filosa no explica bien el patrón.");
    state_claim(d, host, "El derrumbe o la compresión sedimentaria no parecen suficientes.");
    state_claim(d, host, "La lesión podría ser perimortem, no una rotura seca tardía.");

    // With observations + discards + perimortem + a complete blunt-perimortem
    // hypothesis, the defensible conclusion becomes available.
    REQUIRE(has_option(d, "La conclusión defendible es trauma contundente perimortem."));
    choose_text(d, "La conclusión defendible es trauma contundente perimortem.");
    advance_to_choice(d, host);
    CHECK(d.current_node() == "after_solution");
    CHECK(state.get("case.report_ready") == pac::core::StateValue{true});
    CHECK(state.get("case.blunt_trauma_supported") == pac::core::StateValue{true});

    // Close the case.
    choose_text(d, "Entendido. Redacto el informe.");
    for (int i = 0; i < 10 && !d.ended(); ++i) {
        host.speaking = false;
        d.update();
    }
    CHECK(d.ended());
    CHECK(state.get("case.schneider_dialog_done") == pac::core::StateValue{true});
    CHECK(fact_warnings.empty()); // the dialog used only declared facts
}

// CONCERN 1, end to end: examining the real close-up hotspots actually records each
// finding in the notebook (via discover_evidence). Drives the shipped logic.lua
// files through CloseUpRuntime with a real NotebookModel bound, and checks the
// single-source rule (skull + context record), the no_collapse gate (the procedence
// sheet), and that the board/file close-ups record nothing.
TEST_CASE("close-up observations record their findings in the notebook") {
    pac::core::Diagnostics log = quiet();
    pac::core::Scripting s(log);
    pac::core::StateStore state;
    bind_dialog_state(s, state); // get_state / set_state over the StateStore

    auto read_data = [](const std::string& rel) {
        std::ifstream in(std::string(PAC_SOURCE_DIR) + "/games/ingreso_urgente/data/" + rel);
        REQUIRE(in.good());
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };

    NotebookModel model(parse_notebook_yaml(read_data("notebook/notebook.yaml")), state, log);

    sol::state& L = s.lua();
    L.set_function("discover_evidence",
                   [&model](const std::string& id) { return model.discoverEvidence(id); });
    // Close-up talk is fire-and-forget in this harness (no bubble to dismiss), so each
    // hotspot handler runs straight through in a single resume.
    REQUIRE(s.run_string("function talk(_, _) end"));
    bool has_sheet = false;
    L.set_function("has_item", [&has_sheet](const std::string& id) {
        return id == "hoja_procedencia" && has_sheet;
    });
    L.set_function("open_notebook", []() {});
    L.set_function("has_classification",
                   [](const std::string&, const std::string&, const std::string&) { return false; });

    auto examine = [&](pac::pnc::CloseUpRuntime& rt, const std::string& hotspot) {
        const pac::core::ScopeId scope = s.open_scope();
        rt.spawn_hotspot(s, scope, hotspot);
        s.update(0.0f); // resume the handler to completion
        s.cancel_scope(scope);
    };

    // Skull: the three trauma findings. They are gated behind individualization in the
    // redesign, so set the gate first or the trauma hotspots only redirect.
    state.set("act1.remains_individualized", true);
    pac::pnc::CloseUpRuntime skull;
    REQUIRE(skull.load(s, read_data("closeups/lab_skull/logic.lua"),
                       "closeups/lab_skull/logic.lua", log));
    examine(skull, "skull_parietal_injury");
    CHECK(model.hasEvidence("no_cut_marks"));
    examine(skull, "fracture_pattern");
    CHECK(model.hasEvidence("radial_fractures"));
    examine(skull, "skull_sinking");
    CHECK(model.hasEvidence("perimortem_possible"));

    // Context record: heavy_lithic is ungated; no_collapse is gated behind the
    // recovered procedence sheet (Puzzle 3).
    pac::pnc::CloseUpRuntime ctx;
    REQUIRE(ctx.load(s, read_data("closeups/lab_notebook2/logic.lua"),
                     "closeups/lab_notebook2/logic.lua", log));
    examine(ctx, "lithic_object");
    CHECK(model.hasEvidence("heavy_lithic"));

    examine(ctx, "regular_stratigraphy"); // no sheet yet -> no finding
    CHECK_FALSE(model.hasEvidence("no_collapse"));
    has_sheet = true;
    examine(ctx, "regular_stratigraphy"); // with the sheet -> recorded
    CHECK(model.hasEvidence("no_collapse"));

    // Five findings come from close-ups (primary_burial comes from the tutorial beat,
    // not a close-up). The board/file close-ups orient only — they must record NOTHING.
    CHECK(model.discoveredEvidenceCount() == 5);
    pac::pnc::CloseUpRuntime board;
    REQUIRE(board.load(s, read_data("closeups/lab_chalkboard/logic.lua"),
                       "closeups/lab_chalkboard/logic.lua", log));
    examine(board, "no_collapse_note");
    examine(board, "primary_burial_note");
    CHECK(model.discoveredEvidenceCount() == 5); // orientation only, never records
}

// CONCERN 2: the Schneider defence is gated by what is WRITTEN in the notebook, not
// only by which findings were observed. Even with every observation, discard and
// perimortem stated, the winning conclusion is offered ONLY when the notebook
// argument is complete AND its conclusion is the correct one.
TEST_CASE("Schneider resolution is gated by the notebook conclusion + completeness") {
    pac::core::Diagnostics log = quiet();
    pac::core::FilesystemResourceSource source(std::string(PAC_SOURCE_DIR) +
                                               "/games/ingreso_urgente/data");
    pac::core::ResourceCache resources(source, log);

    auto resolution_offered = [&](bool complete, const std::string& conclusion) {
        // Backing values for the notebook stubs, declared BEFORE the Scripting so the
        // sol-bound lambdas can capture them by reference: the functors then hold no
        // heap object to dispose when lua_close finalizes them (a value-captured
        // std::string would be destroyed during teardown and SIGSEGV).
        const bool notebook_complete = complete;
        const std::string notebook_conclusion = conclusion;
        pac::core::Scripting s(log);
        pac::core::StateStore state;
        bind_dialog_state(s, state);
        s.lua().set_function("get_hypothesis_conclusion",
                             [&notebook_conclusion](const std::string& id) -> std::string {
                                 return id == "matilde_trauma" ? notebook_conclusion : std::string();
                             });
        s.lua().set_function("is_hypothesis_complete", [&notebook_complete](const std::string& id) {
            return notebook_complete && id == "matilde_trauma";
        });
        // Findings gathered: these make the observation topics appear (independent of
        // the written argument).
        state.set("finding.radial_fractures", true);
        state.set("finding.no_cut_marks", true);
        state.set("finding.no_collapse", true);
        state.set("finding.perimortem_possible", true);
        const pac::core::FactsRegistry facts =
            pac::core::FactsRegistry::parse(resources.read_text("facts.yaml"));
        pac::core::bind_facts(s, facts, /*dev_warn=*/false, [](const std::string&) {});

        DialogTestHost host;
        auto dopt =
            pac::pnc::DialogRuntime::start(s, resources, log, "skull_trauma_cause", host.host());
        REQUIRE(dopt.has_value());
        pac::pnc::DialogRuntime& d = *dopt;
        advance_to_choice(d, host); // into the hub

        // State the observations, then the discards, then perimortem — all gated by
        // findings, all available here regardless of the notebook stubs.
        state_claim(d, host, "El cráneo muestra una lesión focal con fracturas radiales.");
        state_claim(d, host, "No se ven marcas de corte en la superficie ósea.");
        state_claim(d, host,
                    "El registro contextual no muestra derrumbe ni compresión sedimentaria clara.");
        state_claim(d, host, "Una herramienta filosa no explica bien el patrón.");
        state_claim(d, host, "El derrumbe o la compresión sedimentaria no parecen suficientes.");
        state_claim(d, host, "La lesión podría ser perimortem, no una rotura seca tardía.");
        return has_option(d, "La conclusión defendible es trauma contundente perimortem.");
    };

    CHECK(resolution_offered(true, "blunt_perimortem"));        // notebook done correctly
    CHECK_FALSE(resolution_offered(false, "blunt_perimortem")); // argument incomplete
    CHECK_FALSE(resolution_offered(true, "sharp_force"));       // wrong conclusion
    CHECK_FALSE(resolution_offered(true, ""));                  // no conclusion chosen
}

// delivery_guy.lua (Act 2, Puzzle 4): a node `run` records facts on entry
// (ask_package sets delivery.knows_package), and signing is gated by TWO things —
// the birome item AND the box label classified defensibly in the notebook
// (box_label/ingreso = material_pendiente). Walking the real file proves the node
// run fires and that the dialog is gated by the notebook classification (the Act-2
// echo of the Act-1 lesson).
TEST_CASE("delivery_guy: node run fires and signing is gated by birome + box label") {
    pac::core::Diagnostics log = quiet();
    // Mutable stub backings declared BEFORE the Scripting so the sol lambdas capture
    // them by reference (and the functors hold no heap object to dispose on teardown).
    bool has_birome = true;
    bool labelled = false; // box_label classified as material_pendiente in the notebook
    pac::core::Scripting s(log);
    pac::core::StateStore state;
    bind_dialog_state(s, state);
    s.lua().set_function("has_item",
                         [&has_birome](const std::string& id) { return id == "birome" && has_birome; });
    s.lua().set_function("remove_item", [](const std::string&) {});
    s.lua().set_function(
        "has_classification",
        [&labelled](const std::string& h, const std::string& subj, const std::string& opt) {
            return labelled && h == "box_label" && subj == "ingreso" && opt == "material_pendiente";
        });

    pac::core::FilesystemResourceSource source(std::string(PAC_SOURCE_DIR) +
                                               "/games/ingreso_urgente/data");
    pac::core::ResourceCache resources(source, log);

    DialogTestHost host;
    auto dopt = pac::pnc::DialogRuntime::start(s, resources, log, "delivery_guy", host.host());
    REQUIRE(dopt.has_value());
    pac::pnc::DialogRuntime& d = *dopt;

    CHECK(state.get("delivery.met") == pac::core::StateValue{true}); // on_enter ran

    advance_to_choice(d, host); // intro -> hub
    CHECK_FALSE(state.get("delivery.knows_package").has_value());

    // ask_package's node `run` fires on entry and records the fact.
    choose_text(d, "¿Qué traés exactamente?");
    advance_to_choice(d, host);
    CHECK(state.get("delivery.knows_package") == pac::core::StateValue{true});

    // Birome in hand but the box label not yet classified defensibly: the winning
    // sign-off is hidden; only the "anotalo y firmo" rebote (which sends you to the
    // notebook) is offered.
    CHECK_FALSE(has_option(d, "Listo, firmo. Va como material pendiente de clasificación."));
    CHECK(has_option(d, "Anotalo y firmo, dale."));

    // Classify the box label correctly, then re-enter the hub so options recompute.
    labelled = true;
    choose_text(d, "¿Por qué necesitás una firma?"); // once -> why_signature -> hub
    advance_to_choice(d, host);
    REQUIRE(has_option(d, "Listo, firmo. Va como material pendiente de clasificación."));

    // Signing records delivery.signed (option `run`) and ends the dialog.
    choose_text(d, "Listo, firmo. Va como material pendiente de clasificación.");
    for (int i = 0; i < 12 && !d.ended(); ++i) {
        host.speaking = false;
        d.update();
    }
    CHECK(d.ended());
    CHECK(state.get("delivery.signed") == pac::core::StateValue{true});
}
