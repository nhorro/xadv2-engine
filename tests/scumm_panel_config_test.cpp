#include "engine/pnc/data_error.hpp"
#include "engine/pnc/inventory.hpp"
#include "engine/pnc/scumm_panel.hpp"
#include "engine/pnc/scumm_panel_config.hpp"

#include <doctest/doctest.h>

#include <string>

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

TEST_CASE("scumm panel ignores inventory arrow hitboxes when arrows are disabled") {
    ScummPanel panel(paging_panel_config(InventoryArrowMode::NONE), {100, 100}, nullptr, nullptr);
    const InventoryModel inventory = inventory_with_three_items();

    const PanelIntent intent = panel.click({95.0f, 50.0f}, inventory, {});
    CHECK(intent.kind == PanelIntent::Kind::NONE);
}
