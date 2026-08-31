#include "engine/pnc/data_error.hpp"
#include "engine/pnc/inventory.hpp"
#include "engine/pnc/scumm_panel.hpp"
#include "engine/pnc/scumm_panel_config.hpp"

#include <doctest/doctest.h>

#include <string>
#include <utility>

using namespace pac::pnc;

namespace {

std::string valid_panel_yaml() {
    return R"yaml(
scumm_panel:
  design_size: [1280, 720]
  layout:
    panel:
      rect: [0, 612, 1280, 108]
      padding: [8, 8, 8, 8]
      background:
        type: image
        image: "panel.png"
        opacity: 0.82
        scale_mode: stretch
    command_bar:
      rect: [8, 8, 1264, 28]
      text_align: left
    body:
      rect: [8, 40, 1264, 60]
      gap: 8
    verb_panel:
      rect: [0, 0, 384, 60]
      rows: 3
      columns: 3
      padding: [8, 4, 8, 4]
      cell_gap: [0, 0]
    inventory_panel:
      rect: [392, 0, 872, 60]
      rows: 2
      columns: 4
      padding: [16, 4, 48, 4]
      cell_gap: [8, 0]
    inventory_arrows:
      mode: background_variants
      placement: right
      previous:
        hitbox: [820, 0, 24, 60]
      next:
        hitbox: [844, 0, 24, 60]
  content:
    verbs:
      - open
      - close
      - look_at
      - use
  skin:
    panel:
      background_variants:
        normal: "panel.png"
        inv_next_hover: "panel_next_hover.png"
    command_text:
      font: "fonts/scumm.ttf"
      size: 18
      color: "#222222"
      align: right
    arrows:
      draw:
        previous_text: "<"
        next_text: ">"
        size: 22
        color: "#222222"
  settings_button:
    enabled: true
    position: [0.97, 0.15]
    anchor: center
    size: [32, 32]
    render_mode: panel
    panel:
      label_key: settings_button
      font: "fonts/scumm.ttf"
      font_size: 14
      normal_color: "#dddddd"
      hovered_color: "#ffffff"
)yaml";
}

ScummPanelConfig paging_panel_config(InventoryArrowMode arrow_mode) {
    ScummPanelConfig cfg;
    cfg.layout.design_size = {100.0f, 100.0f};
    cfg.layout.panel_rect = {0.0f, 0.0f, 100.0f, 100.0f};
    cfg.layout.command_bar_rect = {0.0f, 0.0f, 100.0f, 10.0f};
    cfg.layout.body_rect = {0.0f, 10.0f, 100.0f, 90.0f};
    cfg.layout.verb_panel.rect = {0.0f, 0.0f, 20.0f, 90.0f};
    cfg.layout.verb_panel.rows = 1;
    cfg.layout.verb_panel.columns = 1;
    cfg.layout.inventory_panel.rect = {20.0f, 0.0f, 80.0f, 90.0f};
    cfg.layout.inventory_panel.rows = 1;
    cfg.layout.inventory_panel.columns = 2;
    cfg.layout.inventory_panel.padding = {0.0f, 0.0f, 20.0f, 0.0f};
    cfg.layout.inventory_arrows.mode = arrow_mode;
    cfg.layout.inventory_arrows.previous_hitbox = {60.0f, 0.0f, 10.0f, 90.0f};
    cfg.layout.inventory_arrows.next_hitbox = {70.0f, 0.0f, 10.0f, 90.0f};
    cfg.content.verbs = {Verb::OPEN};
    return cfg;
}

InventoryModel inventory_with_three_items() {
    InventoryModel inventory;
    inventory.replace_all({"key", "map", "coin"});
    return inventory;
}

} // namespace

TEST_CASE("scumm panel config parses layout, skin, and relative asset paths") {
    const ScummPanelConfig cfg = parse_scumm_panel_config(valid_panel_yaml(), "ui/scumm_panel.yml");

    CHECK(cfg.layout.design_size.x == doctest::Approx(1280.0f));
    CHECK(cfg.layout.design_size.y == doctest::Approx(720.0f));
    CHECK(cfg.layout.panel_rect.top == doctest::Approx(612.0f));
    CHECK(cfg.layout.background.type == ScummPanelBackgroundType::IMAGE);
    CHECK(cfg.layout.background.image == "ui/panel.png");
    CHECK(cfg.layout.background.opacity == doctest::Approx(0.82f));
    CHECK(cfg.layout.command_bar_rect.left == doctest::Approx(8.0f));
    CHECK(cfg.layout.inventory_panel.rows == 2);
    CHECK(cfg.layout.inventory_panel.columns == 4);
    CHECK(cfg.layout.inventory_arrows.mode == InventoryArrowMode::BACKGROUND_VARIANTS);

    REQUIRE(cfg.content.verbs.size() == 4);
    CHECK(cfg.content.verbs[0] == Verb::OPEN);
    CHECK(cfg.content.verbs[1] == Verb::CLOSE);
    CHECK(cfg.content.verbs[2] == Verb::LOOK_AT);
    CHECK(cfg.content.verbs[3] == Verb::USE);

    CHECK(cfg.skin.background_variants.at("normal") == "ui/panel.png");
    CHECK(cfg.skin.background_variants.at("inv_next_hover") == "ui/panel_next_hover.png");
    CHECK(cfg.skin.command_text.font == "ui/fonts/scumm.ttf");
    CHECK(cfg.skin.command_text.align == "right");

    CHECK(cfg.settings_button.enabled);
    CHECK(cfg.settings_button.position.x == doctest::Approx(0.97f));
    CHECK(cfg.settings_button.position.y == doctest::Approx(0.15f));
    CHECK(cfg.settings_button.size.x == doctest::Approx(32.0f));
    CHECK(cfg.settings_button.anchor == ScummPanelAnchor::CENTER);
    CHECK(cfg.settings_button.render_mode == ScummButtonRenderMode::PANEL);
    CHECK(cfg.settings_button.panel.label_key == "settings_button");
    CHECK(cfg.settings_button.panel.font == "ui/fonts/scumm.ttf");
}

TEST_CASE("scumm panel config rejects too many verbs for the grid") {
    const std::string yaml = R"yaml(
scumm_panel:
  design_size: [1280, 720]
  layout:
    panel:
      rect: [0, 612, 1280, 108]
      background:
        type: solid
        color: "#000000"
    command_bar:
      rect: [0, 0, 1280, 32]
    body:
      rect: [0, 32, 1280, 76]
    verb_panel:
      rect: [0, 0, 200, 76]
      rows: 1
      columns: 1
    inventory_panel:
      rect: [200, 0, 1080, 76]
      rows: 1
      columns: 1
  content:
    verbs: [open, close]
)yaml";

    CHECK_THROWS_AS((void) parse_scumm_panel_config(yaml), DataError);
}

TEST_CASE("scumm panel config rejects invalid arrow modes") {
    std::string yaml = valid_panel_yaml();
    const std::string needle = "mode: background_variants";
    const std::size_t pos = yaml.find(needle);
    REQUIRE(pos != std::string::npos);
    yaml.replace(pos, needle.size(), "mode: sideways");

    CHECK_THROWS_AS((void) parse_scumm_panel_config(yaml), DataError);
}

TEST_CASE("scumm panel config requires a normal background for background variant arrows") {
    const std::string yaml = R"yaml(
scumm_panel:
  design_size: [1280, 720]
  layout:
    panel:
      rect: [0, 612, 1280, 108]
      background:
        type: solid
        color: "#000000"
    command_bar:
      rect: [0, 0, 1280, 32]
    body:
      rect: [0, 32, 1280, 76]
    verb_panel:
      rect: [0, 0, 200, 76]
      rows: 1
      columns: 1
    inventory_panel:
      rect: [200, 0, 1080, 76]
      rows: 1
      columns: 1
    inventory_arrows:
      mode: background_variants
      previous:
        hitbox: [1000, 0, 24, 76]
      next:
        hitbox: [1030, 0, 24, 76]
  content:
    verbs: [open]
)yaml";

    CHECK_THROWS_AS((void) parse_scumm_panel_config(yaml), DataError);
}

TEST_CASE("scumm panel config parses image settings button assets relative to the YAML") {
    const std::string yaml = R"yaml(
scumm_panel:
  design_size: [1280, 720]
  layout:
    panel:
      rect: [0, 612, 1280, 108]
      background:
        type: solid
        color: "#000000"
    command_bar:
      rect: [0, 0, 1280, 32]
    body:
      rect: [0, 32, 1280, 76]
    verb_panel:
      rect: [0, 0, 200, 76]
      rows: 1
      columns: 1
    inventory_panel:
      rect: [200, 0, 1080, 76]
      rows: 1
      columns: 1
  content:
    verbs: [open]
  settings_button:
    enabled: true
    position: [1, 0]
    anchor: top_right
    size: [32, 32]
    render_mode: image
    image:
      normal: "settings.png"
      hovered: "settings_hover.png"
)yaml";

    const ScummPanelConfig cfg = parse_scumm_panel_config(yaml, "ui/panel.yml");

    CHECK(cfg.settings_button.enabled);
    CHECK(cfg.settings_button.anchor == ScummPanelAnchor::TOP_RIGHT);
    CHECK(cfg.settings_button.render_mode == ScummButtonRenderMode::IMAGE);
    CHECK(cfg.settings_button.image.normal == "ui/settings.png");
    CHECK(cfg.settings_button.image.hovered == "ui/settings_hover.png");
}

TEST_CASE("scumm panel config rejects image settings buttons without a normal image") {
    std::string yaml = valid_panel_yaml();
    const std::string needle = "render_mode: panel";
    const std::size_t pos = yaml.find(needle);
    REQUIRE(pos != std::string::npos);
    yaml.replace(pos, needle.size(), "render_mode: image");

    CHECK_THROWS_AS((void) parse_scumm_panel_config(yaml), DataError);
}

TEST_CASE("scumm panel emits inventory page intents and uses the visible page for item clicks") {
    ScummPanel panel(paging_panel_config(InventoryArrowMode::DRAW), {100, 100}, nullptr, nullptr);
    const InventoryModel inventory = inventory_with_three_items();

    CommandState state;
    PanelIntent intent = panel.click({95.0f, 50.0f}, inventory, state);
    CHECK(intent.kind == PanelIntent::Kind::CHANGE_INVENTORY_PAGE);
    CHECK(intent.page_index == 1);

    state.inventory_page_index = 1;
    intent = panel.click({85.0f, 50.0f}, inventory, state);
    CHECK(intent.kind == PanelIntent::Kind::CHANGE_INVENTORY_PAGE);
    CHECK(intent.page_index == 0);

    intent = panel.click({25.0f, 50.0f}, inventory, state);
    CHECK(intent.kind == PanelIntent::Kind::CLICK_INVENTORY);
    CHECK(intent.item_id == "coin");
}

TEST_CASE("scumm panel settings button consumes clicks before command controls") {
    ScummPanelConfig cfg = paging_panel_config(InventoryArrowMode::DRAW);
    cfg.settings_button.enabled = true;
    cfg.settings_button.position = {0.95f, 0.5f};
    cfg.settings_button.size = {10.0f, 10.0f};
    cfg.settings_button.anchor = ScummPanelAnchor::CENTER;
    ScummPanel panel(std::move(cfg), {100, 100}, nullptr, nullptr);
    const InventoryModel inventory = inventory_with_three_items();

    const PanelIntent intent = panel.click({95.0f, 50.0f}, inventory, {});
    CHECK(intent.kind == PanelIntent::Kind::OPEN_SETTINGS);
}

TEST_CASE("scumm panel ignores inventory arrow hitboxes when arrows are disabled") {
    ScummPanel panel(paging_panel_config(InventoryArrowMode::NONE), {100, 100}, nullptr, nullptr);
    const InventoryModel inventory = inventory_with_three_items();

    const PanelIntent intent = panel.click({95.0f, 50.0f}, inventory, {});
    CHECK(intent.kind == PanelIntent::Kind::NONE);
}

namespace {

// A v2 IU-style panel: text verbs, icon inventory, evidence indicator, and a
// two-entry notebook. 100x100 design so click coords map 1:1 at runtime 100x100.
std::string v2_panel_yaml() {
    return R"yaml(
scumm_panel:
  design_size: [100, 100]
  layout:
    panel:
      rect: [0, 0, 100, 100]
      background:
        type: solid
        color: "#000000"
    command_bar:
      rect: [0, 0, 100, 10]
    body:
      rect: [0, 10, 100, 90]
    verb_panel:
      rect: [0, 0, 40, 40]
      rows: 1
      columns: 1
      style: text
    inventory_panel:
      rect: [40, 0, 60, 40]
      rows: 1
      columns: 4
      style: icons
  content:
    verbs: [look_at, talk_to, pick_up, use]
  evidence_indicator:
    enabled: true
    rect: [0, 45, 40, 20]
    label_key: evidencias
    collected_state: notebook.evidence.collected
    total_state: notebook.evidence.total
  notebook:
    enabled: true
    rect: [0, 70, 100, 20]
    scene: notebook
    tab_state: notebook.initial_tab
    entries:
      - { label_key: notebook, tab: evidence }
      - { label_key: hipotesis, tab: hypotheses }
  skin:
    # A skin with text styles but no "panel"/"arrows" children: the icon inventory
    # needs no background variants or paging arrows. Regression: indexing
    # skin["panel"]["background_variants"] must not throw yaml-cpp InvalidNode.
    command_text:
      size: 20
      color: "#f2e0b0"
    verb_text:
      size: 18
      color: "#9be29b"
)yaml";
}

} // namespace

TEST_CASE("scumm panel v2 config parses verb/inventory styles, evidence, and notebook") {
    const ScummPanelConfig cfg = parse_scumm_panel_config(v2_panel_yaml(), "ui/panel.yml");

    CHECK(cfg.layout.verb_style == VerbPanelStyle::TEXT);
    CHECK(cfg.layout.inventory_style == InventoryStyle::ICONS);

    CHECK(cfg.evidence_indicator.enabled);
    CHECK(cfg.evidence_indicator.label_key == "evidencias");
    CHECK(cfg.evidence_indicator.collected_state == "notebook.evidence.collected");
    CHECK(cfg.evidence_indicator.total_state == "notebook.evidence.total");

    CHECK(cfg.notebook.enabled);
    CHECK(cfg.notebook.scene == "notebook");
    CHECK(cfg.notebook.tab_state == "notebook.initial_tab");
    REQUIRE(cfg.notebook.entries.size() == 2);
    CHECK(cfg.notebook.entries[0].label_key == "notebook");
    CHECK(cfg.notebook.entries[0].tab == "evidence");
    CHECK(cfg.notebook.entries[1].label_key == "hipotesis");
    CHECK(cfg.notebook.entries[1].tab == "hypotheses");
}

TEST_CASE("text verb style is not bound by the grid capacity") {
    // 1x1 grid but four verbs: rejected for BUTTONS, accepted for TEXT.
    std::string yaml = v2_panel_yaml();
    const ScummPanelConfig text_cfg = parse_scumm_panel_config(yaml, "ui/panel.yml");
    CHECK(text_cfg.content.verbs.size() == 4);

    const std::string needle = "style: text";
    const std::size_t pos = yaml.find(needle);
    REQUIRE(pos != std::string::npos);
    yaml.replace(pos, needle.size(), "style: buttons");
    CHECK_THROWS_AS((void) parse_scumm_panel_config(yaml, "ui/panel.yml"), DataError);
}

TEST_CASE("scumm panel notebook zones emit OPEN_NOTEBOOK with the entry's tab") {
    const ScummPanelConfig cfg = parse_scumm_panel_config(v2_panel_yaml(), "ui/panel.yml");
    ScummPanel panel(cfg, {100, 100}, nullptr, nullptr);
    const InventoryModel inventory; // empty

    // Notebook area is body-relative {0,70,100,20} -> runtime {0,80,100,20}; a
    // leading icon gutter (square, ~20 wide) sits at the left, and the two entries
    // stack vertically to its right: top row = Cuaderno (evidence), bottom row =
    // Hipótesis (hypotheses).
    const PanelIntent top = panel.click({60.0f, 83.0f}, inventory, {});
    CHECK(top.kind == PanelIntent::Kind::OPEN_NOTEBOOK);
    CHECK(top.tab == "evidence");

    const PanelIntent bottom = panel.click({60.0f, 93.0f}, inventory, {});
    CHECK(bottom.kind == PanelIntent::Kind::OPEN_NOTEBOOK);
    CHECK(bottom.tab == "hypotheses");

    // A click inside the icon gutter (left of the entries) is not an entry.
    const PanelIntent gutter = panel.click({5.0f, 85.0f}, inventory, {});
    CHECK(gutter.kind != PanelIntent::Kind::OPEN_NOTEBOOK);
}

TEST_CASE("icon inventory pages with vertical arrows") {
    ScummPanelConfig cfg = paging_panel_config(InventoryArrowMode::NONE);
    cfg.layout.inventory_style = InventoryStyle::ICONS;
    cfg.layout.inventory_panel.padding = {0.0f, 0.0f, 0.0f, 0.0f};
    // inventory rect {20,0,80,90}, rows 1 x columns 2 -> capacity 2; three items
    // need two pages.
    ScummPanel panel(std::move(cfg), {100, 100}, nullptr, nullptr);
    const InventoryModel inventory = inventory_with_three_items();

    CommandState state;
    // The next (down) arrow is the bottom half of the right-hand gutter.
    PanelIntent intent = panel.click({92.0f, 75.0f}, inventory, state);
    CHECK(intent.kind == PanelIntent::Kind::CHANGE_INVENTORY_PAGE);
    CHECK(intent.page_index == 1);

    // From page 1 the prev (up) arrow (top half) returns to page 0.
    state.inventory_page_index = 1;
    intent = panel.click({92.0f, 30.0f}, inventory, state);
    CHECK(intent.kind == PanelIntent::Kind::CHANGE_INVENTORY_PAGE);
    CHECK(intent.page_index == 0);

    // A slot click on page 0 selects the first item.
    intent = panel.click({30.0f, 50.0f}, inventory, {});
    CHECK(intent.kind == PanelIntent::Kind::CLICK_INVENTORY);
    CHECK(intent.item_id == "key");
}

TEST_CASE("scumm panel text verbs lay out horizontally and emit SELECT_VERB") {
    const ScummPanelConfig cfg = parse_scumm_panel_config(v2_panel_yaml(), "ui/panel.yml");
    ScummPanel panel(cfg, {100, 100}, nullptr, nullptr);
    const InventoryModel inventory;

    // Verb area {0,10,40,40}; four equal columns of width 10. Column 0 center x=5.
    const PanelIntent first = panel.click({5.0f, 20.0f}, inventory, {});
    CHECK(first.kind == PanelIntent::Kind::SELECT_VERB);
    CHECK(first.verb == Verb::LOOK_AT);
    // Column 3 center x=35 -> the fourth verb (use).
    const PanelIntent fourth = panel.click({35.0f, 20.0f}, inventory, {});
    CHECK(fourth.kind == PanelIntent::Kind::SELECT_VERB);
    CHECK(fourth.verb == Verb::USE);
}

namespace {

// A spec-style panel (issue: SCUMM UI v1.0): a 3x3 verb grid, a dedicated
// pagination column left of an eight-slot icon inventory, and a two-button
// systemic controls column. 128x72 design so click coords map 1:1 at that runtime
// size (a tenth of the real 1280x720 geometry).
std::string systemic_panel_yaml() {
    return R"yaml(
scumm_panel:
  design_size: [128, 72]
  layout:
    panel:
      rect: [0, 59.2, 128, 12.8]
      padding: [0, 0, 0, 0]
      background:
        type: solid
        color: "#151617"
    command_bar:
      rect: [0, 0, 128, 3.2]
    body:
      rect: [0, 3.2, 128, 9.6]
    verb_panel:
      rect: [0, 0, 36, 9.6]
      rows: 3
      columns: 3
      style: buttons
    inventory_pagination:
      rect: [36, 0, 4, 9.6]
      previous: [0, 0, 4, 4.8]
      next: [0, 4.8, 4, 4.8]
    inventory_panel:
      rect: [40, 0, 76.8, 9.6]
      rows: 1
      columns: 8
      style: icons
      # Explicit: the engine's grid defaults carry the classic layout's 52px right
      # padding (gutter for the old arrows) and 8px cell gap, which would eat the
      # slots here.
      padding: [0, 0, 0, 0]
      cell_gap: [0, 0]
  content:
    verbs: [look_at, open, push, talk_to, close, pull, pick_up, use, give]
  system_buttons:
    - id: options
      rect: [116.8, 0, 11.2, 4.8]
      action: open_settings
      label_key: settings
    - id: menu
      rect: [116.8, 4.8, 11.2, 4.8]
      action: open_menu
      label_key: menu
  skin:
    verb_button:
      background: "#16212B"
      border: "#3A4650"
      hover_border: "#2BB7D6"
      selected_border: "#C9982E"
      disabled_background: "#16212B80"
    verb_text:
      color: "#E1D1AB"
      hover_color: "#2BB7D6"
      selected_color: "#C9982E"
      disabled_color: "#B0A58A"
)yaml";
}

} // namespace

TEST_CASE("scumm panel parses button skins, alpha colors, and the selected text color") {
    const ScummPanelConfig cfg = parse_scumm_panel_config(systemic_panel_yaml(), "ui/panel.yml");

    CHECK(cfg.skin.verb_button.background == sf::Color(0x16, 0x21, 0x2B));
    CHECK(cfg.skin.verb_button.border == sf::Color(0x3A, 0x46, 0x50));
    CHECK(cfg.skin.verb_button.hover_border == sf::Color(0x2B, 0xB7, 0xD6));
    CHECK(cfg.skin.verb_button.selected_border == sf::Color(0xC9, 0x98, 0x2E));
    // #RRGGBBAA keeps its alpha; an unset state falls back to the engine default.
    CHECK(cfg.skin.verb_button.disabled_background == sf::Color(0x16, 0x21, 0x2B, 0x80));
    CHECK(cfg.skin.verb_button.hover_background == ScummButtonSkin{}.hover_background);

    CHECK(cfg.skin.verb_text.selected_color == sf::Color(0xC9, 0x98, 0x2E));
    CHECK(cfg.skin.verb_text.disabled_color == sf::Color(0xB0, 0xA5, 0x8A));
}

TEST_CASE("scumm panel systemic buttons emit their configured action") {
    const ScummPanelConfig cfg = parse_scumm_panel_config(systemic_panel_yaml(), "ui/panel.yml");
    REQUIRE(cfg.system_buttons.size() == 2);
    CHECK(cfg.system_buttons[0].id == "options");
    CHECK(cfg.system_buttons[1].action == ScummSystemAction::OPEN_MENU);

    ScummPanel panel(cfg, {128, 72}, nullptr, nullptr);
    const InventoryModel inventory = inventory_with_three_items();

    // Body starts at panel.top + 3.2 = 62.4. The controls column is x 116.8..128;
    // "Opciones" is the top half (y 62.4..67.2), "Menú" the bottom (67.2..72).
    const PanelIntent options = panel.click({122.0f, 64.0f}, inventory, {});
    CHECK(options.kind == PanelIntent::Kind::OPEN_SETTINGS);

    const PanelIntent menu = panel.click({122.0f, 70.0f}, inventory, {});
    CHECK(menu.kind == PanelIntent::Kind::OPEN_MENU);
}

TEST_CASE("scumm panel push_scene systemic buttons carry the scene id") {
    ScummPanelConfig cfg = parse_scumm_panel_config(systemic_panel_yaml(), "ui/panel.yml");
    cfg.system_buttons[1].action = ScummSystemAction::PUSH_SCENE;
    cfg.system_buttons[1].scene = "journal";
    ScummPanel panel(std::move(cfg), {128, 72}, nullptr, nullptr);

    const PanelIntent intent = panel.click({122.0f, 70.0f}, InventoryModel{}, {});
    CHECK(intent.kind == PanelIntent::Kind::PUSH_SCENE);
    CHECK(intent.scene == "journal");
}

TEST_CASE("scumm panel rejects push_scene systemic buttons without a scene") {
    const std::string yaml = R"yaml(
scumm_panel:
  design_size: [128, 72]
  layout:
    panel:
      rect: [0, 59.2, 128, 12.8]
    command_bar:
      rect: [0, 0, 128, 3.2]
    body:
      rect: [0, 3.2, 128, 9.6]
  system_buttons:
    - id: journal
      rect: [116.8, 0, 11.2, 4.8]
      action: push_scene
      label_key: journal
)yaml";
    CHECK_THROWS_AS(static_cast<void>(parse_scumm_panel_config(yaml, "ui/panel.yml")), DataError);
}

TEST_CASE("explicit inventory pagination pages the icon inventory and frees the whole rect") {
    const ScummPanelConfig cfg = parse_scumm_panel_config(systemic_panel_yaml(), "ui/panel.yml");
    REQUIRE(cfg.layout.inventory_pagination.enabled);

    ScummPanel panel(cfg, {128, 72}, nullptr, nullptr);
    // Capacity is 8; ten items need two pages.
    InventoryModel inventory;
    inventory.replace_all({"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"});

    // The pagination column is body-relative x 36..40, i.e. left of the inventory.
    // Next = bottom half (y 67.2..72), previous = top half (62.4..67.2).
    CommandState state;
    const PanelIntent next = panel.click({38.0f, 70.0f}, inventory, state);
    CHECK(next.kind == PanelIntent::Kind::CHANGE_INVENTORY_PAGE);
    CHECK(next.page_index == 1);

    state.inventory_page_index = 1;
    const PanelIntent prev = panel.click({38.0f, 64.0f}, inventory, state);
    CHECK(prev.kind == PanelIntent::Kind::CHANGE_INVENTORY_PAGE);
    CHECK(prev.page_index == 0);

    // With paging outside the inventory rect, no gutter is carved out of it: the
    // eighth slot spans x 116.8-9.6..116.8 and is clickable to its right edge.
    const PanelIntent last_slot = panel.click({116.0f, 67.0f}, inventory, {});
    CHECK(last_slot.kind == PanelIntent::Kind::CLICK_INVENTORY);
    CHECK(last_slot.item_id == "h");

    // Page 1 shows the remaining two items in the first slots.
    state.inventory_page_index = 1;
    const PanelIntent page2 = panel.click({42.0f, 67.0f}, inventory, state);
    CHECK(page2.kind == PanelIntent::Kind::CLICK_INVENTORY);
    CHECK(page2.item_id == "i");
}

TEST_CASE("inventory pagination requires the icon inventory style") {
    const std::string yaml = R"yaml(
scumm_panel:
  design_size: [128, 72]
  layout:
    panel:
      rect: [0, 59.2, 128, 12.8]
    command_bar:
      rect: [0, 0, 128, 3.2]
    body:
      rect: [0, 3.2, 128, 9.6]
    inventory_panel:
      rect: [40, 0, 76.8, 9.6]
      rows: 1
      columns: 8
      style: text
    inventory_pagination:
      rect: [36, 0, 4, 9.6]
      previous: [0, 0, 4, 4.8]
      next: [0, 4.8, 4, 4.8]
)yaml";
    CHECK_THROWS_AS(static_cast<void>(parse_scumm_panel_config(yaml, "ui/panel.yml")), DataError);
}

TEST_CASE("scumm panel command bar skin overrides the classic strip, and defaults to it") {
    const ScummPanelConfig fallback =
        parse_scumm_panel_config(systemic_panel_yaml(), "ui/panel.yml");
    CHECK(fallback.skin.command_bar.background == ScummCommandBarSkin{}.background);
    CHECK(fallback.skin.command_bar.separator == ScummCommandBarSkin{}.separator);

    const std::string yaml = R"yaml(
scumm_panel:
  design_size: [128, 72]
  layout:
    panel:
      rect: [0, 59.2, 128, 12.8]
    command_bar:
      rect: [0, 0, 128, 3.2]
    body:
      rect: [0, 3.2, 128, 9.6]
  skin:
    command_bar:
      background: "#151617"
      separator: "#3A4650"
      separator_thickness: 0
)yaml";
    const ScummPanelConfig cfg = parse_scumm_panel_config(yaml, "ui/panel.yml");
    CHECK(cfg.skin.command_bar.background == sf::Color(0x15, 0x16, 0x17));
    CHECK(cfg.skin.command_bar.separator == sf::Color(0x3A, 0x46, 0x50));
    CHECK(cfg.skin.command_bar.separator_thickness == doctest::Approx(0.0f));
}
