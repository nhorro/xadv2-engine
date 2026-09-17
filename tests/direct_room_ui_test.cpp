#include "engine/pnc/direct_room_ui_config.hpp"
#include "engine/pnc/direct_room_widget.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace pac::pnc;

namespace {

DirectRoomUiConfig test_config() {
    DirectRoomUiConfig config;
    config.design_size = {100.0f, 100.0f};
    config.bag_button = {80.0f, 80.0f, 10.0f, 10.0f};
    config.menu_button = {90.0f, 80.0f, 10.0f, 10.0f};
    config.action_text = {20.0f, 90.0f, 60.0f, 8.0f};
    config.inventory.panel = {5.0f, 5.0f, 90.0f, 70.0f};
    config.inventory.grid = {10.0f, 10.0f, 80.0f, 40.0f};
    config.inventory.previous = {10.0f, 55.0f, 20.0f, 10.0f};
    config.inventory.next = {70.0f, 55.0f, 20.0f, 10.0f};
    config.inventory.rows = 1;
    config.inventory.columns = 2;
    config.inventory.cell_gap = {4.0f, 4.0f};
    config.interaction.drag_threshold = 5.0f;
    config.interaction.long_press_seconds = 0.45f;
    config.context_menu.cell_size = {20.0f, 15.0f};
    config.context_menu.gap = 2.0f;
    config.context_menu.padding = 2.0f;
    return config;
}

struct Fixture {
    RoomUiState state;
    RoomUiStateStream stream;
    std::vector<RoomUiIntent> intents;
    DirectRoomWidget widget;

    Fixture()
        : widget(test_config(), {100, 100}, nullptr, {}, [this](const RoomUiIntent& intent) {
              intents.push_back(intent);
          }) {
        InventoryItem key;
        key.id = "key";
        InventoryItem coin;
        coin.id = "coin";
        InventoryItem map;
        map.id = "map";
        state.inventory.set_definitions({{"key", key}, {"coin", coin}, {"map", map}});
        state.inventory.replace_all({"key", "coin", "map"});
        widget.connect(stream);
        publish(false);
    }

    void publish(bool open) {
        state.mode = RoomInteractionMode::COMMAND;
        state.inventory_open = open;
        stream.publish(state);
    }

    void press(float x, float y) {
        (void) widget.handle({RoutedInputKind::PRIMARY_PRESSED, {x, y}});
    }

    void release(float x, float y) {
        (void) widget.handle({RoutedInputKind::PRIMARY_RELEASED, {x, y}});
    }
};

} // namespace

TEST_CASE("direct room UI config parses gesture, layout, and style values") {
    const DirectRoomUiConfig config = parse_direct_room_ui_config(R"yaml(
direct_room_ui:
  design_size: [100, 80]
  interaction:
    drag_threshold: 4
    long_press_seconds: 0.42
  bag_button: [80, 60, 10, 10]
  menu_button: [90, 60, 10, 10]
  action_text: [20, 65, 60, 10]
  bag_label: inventory
  menu_label: options
  inventory:
    panel: [10, 10, 80, 45]
    grid: [15, 15, 70, 25]
    previous: [15, 42, 10, 8]
    next: [75, 42, 10, 8]
    rows: 1
    columns: 4
    cell_gap: [2, 0]
    compact_single_page: true
    compact_padding: 7
  icons:
    sheet: shared/ui/actions.png
    columns: 4
    rows: 4
    bag: 3
  style:
    panel_background: "#101112CC"
    text: "#AABBCC"
    text_size: 18
    button_radius_ratio: 0.4
)yaml");

    CHECK(config.design_size == sf::Vector2f(100.0f, 80.0f));
    CHECK(config.interaction.long_press_seconds == doctest::Approx(0.42f));
    CHECK(config.inventory.columns == 4);
    CHECK(config.inventory.cell_gap.y == doctest::Approx(0.0f));
    CHECK(config.inventory.compact_single_page);
    CHECK(config.inventory.compact_padding == doctest::Approx(7.0f));
    CHECK(config.icons.sheet == "shared/ui/actions.png");
    CHECK(config.icons.bag == 3);
    CHECK(config.bag_label == "inventory");
    CHECK(config.menu_label == "options");
    CHECK(config.style.panel_background == sf::Color(16, 17, 18, 204));
    CHECK(config.style.text == sf::Color(170, 187, 204, 255));
    CHECK(config.style.text_size == 18);
    CHECK(config.style.button_radius_ratio == doctest::Approx(0.4f));
}

TEST_CASE("direct inventory is hidden by default and bag and menu remain actionable") {
    Fixture fixture;
    CHECK_FALSE(fixture.widget.inventory_item_at({20.0f, 20.0f}));
    CHECK(fixture.widget.captures({85.0f, 85.0f}));
    CHECK(fixture.widget.captures({95.0f, 85.0f}));
    CHECK_FALSE(fixture.widget.captures({20.0f, 20.0f}));
    CHECK(fixture.widget.handle({RoutedInputKind::POINTER_MOVED, {20.0f, 20.0f}}) ==
          InputResult::PASS);

    fixture.release(85.0f, 85.0f);
    REQUIRE(fixture.intents.size() == 1);
    CHECK(fixture.intents.back().kind == RoomUiIntent::Kind::TOGGLE_INVENTORY);

    fixture.release(95.0f, 85.0f);
    REQUIRE(fixture.intents.size() == 2);
    CHECK(fixture.intents.back().kind == RoomUiIntent::Kind::OPEN_MENU);
}

TEST_CASE("inventory item hover proposes examination and consumes scenery hover") {
    Fixture fixture;
    fixture.publish(true);
    CHECK(fixture.widget.handle({RoutedInputKind::POINTER_MOVED, {20.0f, 20.0f}}) ==
          InputResult::CONSUMED);

    REQUIRE(fixture.intents.size() == 1);
    CHECK(fixture.intents.back().kind == RoomUiIntent::Kind::HOVER_INVENTORY_ITEM);
    CHECK(fixture.intents.back().id == "key");

    fixture.state.command.hover = CommandHoverKind::OBJECT;
    fixture.state.command.hovered_object = ObjectRef{ObjectKind::INVENTORY_OBJECT, "key"};
    fixture.publish(true);
    CHECK(fixture.widget.handle({RoutedInputKind::POINTER_MOVED, {50.0f, 70.0f}}) ==
          InputResult::CONSUMED);
    REQUIRE(fixture.intents.size() == 2);
    CHECK(fixture.intents.back().kind == RoomUiIntent::Kind::CLEAR_COMMAND_HOVER);
}

TEST_CASE("leaving an inventory item clears its proposal and passes hover to the room") {
    Fixture fixture;
    fixture.publish(true);
    fixture.state.command.hover = CommandHoverKind::OBJECT;
    fixture.state.command.hovered_object = ObjectRef{ObjectKind::INVENTORY_OBJECT, "key"};
    fixture.publish(true);

    CHECK(fixture.widget.handle({RoutedInputKind::POINTER_MOVED, {1.0f, 1.0f}}) ==
          InputResult::PASS);
    REQUIRE(fixture.intents.size() == 1);
    CHECK(fixture.intents.back().kind == RoomUiIntent::Kind::CLEAR_COMMAND_HOVER);
}

TEST_CASE("single inventory tap immediately invokes the authored default action") {
    Fixture fixture;
    fixture.publish(true);
    fixture.press(20.0f, 20.0f);
    fixture.release(20.0f, 20.0f);
    REQUIRE(fixture.intents.size() == 1);
    CHECK(fixture.intents.back().kind == RoomUiIntent::Kind::INTERACT_INVENTORY_ITEM);
    CHECK(fixture.intents.back().id == "key");
}

TEST_CASE("compact single-page inventory fits and right-aligns its occupied cells") {
    DirectRoomUiConfig config = test_config();
    config.inventory.compact_single_page = true;
    config.inventory.compact_padding = 5.0f;
    RoomUiState state;
    state.mode = RoomInteractionMode::COMMAND;
    state.inventory_open = true;
    InventoryItem key;
    key.id = "key";
    state.inventory.set_definitions({{"key", key}});
    state.inventory.replace_all({"key"});
    RoomUiStateStream stream;
    DirectRoomWidget widget(config, {100, 100}, nullptr, {}, [](const RoomUiIntent&) {});
    widget.connect(stream);
    stream.publish(state);

    CHECK_FALSE(widget.inventory_item_at({20.0f, 20.0f}));
    const auto item = widget.inventory_item_at({60.0f, 20.0f});
    REQUIRE(item);
    CHECK(*item == "key");
    CHECK_FALSE(widget.captures({40.0f, 20.0f}));
    CHECK(widget.captures({60.0f, 20.0f}));
}

TEST_CASE("inventory drag captures outside the panel and emits a screen-space drop") {
    Fixture fixture;
    fixture.publish(true);
    fixture.press(20.0f, 20.0f);
    CHECK(fixture.widget.handle({RoutedInputKind::POINTER_MOVED, {60.0f, 90.0f}}) ==
          InputResult::CONSUMED);
    CHECK(fixture.widget.dragging());
    CHECK(fixture.widget.captures({1.0f, 1.0f}));
    CHECK_FALSE(fixture.widget.contains_ui({1.0f, 1.0f}));
    CHECK(fixture.widget.contains_ui({85.0f, 85.0f}));
    fixture.release(2.0f, 3.0f);

    REQUIRE(fixture.intents.size() == 2);
    CHECK(fixture.intents.front().kind == RoomUiIntent::Kind::PREVIEW_INVENTORY_DROP);
    CHECK(fixture.intents.front().id == "key");
    CHECK(fixture.intents.front().position == sf::Vector2f(60.0f, 90.0f));
    CHECK(fixture.intents.back().kind == RoomUiIntent::Kind::DROP_INVENTORY_ITEM);
    CHECK(fixture.intents.back().id == "key");
    CHECK(fixture.intents.back().position == sf::Vector2f(2.0f, 3.0f));
    CHECK_FALSE(fixture.widget.dragging());
}

TEST_CASE("closing inventory or leaving command mode cancels direct gestures") {
    Fixture fixture;
    fixture.publish(true);
    fixture.press(20.0f, 20.0f);
    fixture.publish(false);
    CHECK(fixture.intents.empty());

    fixture.publish(true);
    fixture.press(20.0f, 20.0f);
    (void) fixture.widget.handle({RoutedInputKind::POINTER_MOVED, {60.0f, 90.0f}});
    REQUIRE(fixture.widget.dragging());
    fixture.state.mode = RoomInteractionMode::MENU;
    fixture.stream.publish(fixture.state);
    CHECK_FALSE(fixture.widget.dragging());
    CHECK_FALSE(fixture.widget.captures({85.0f, 85.0f}));
}

TEST_CASE("context menu captures the room and emits the selected secondary action") {
    Fixture fixture;
    fixture.state.context_menu.target = {ObjectKind::ROOM_OBJECT, "door"};
    fixture.state.context_menu.position = {50.0f, 60.0f};
    fixture.state.context_menu.actions = {Verb::LOOK_AT, Verb::OPEN};
    fixture.publish(false);

    CHECK(fixture.widget.captures({1.0f, 1.0f}));
    const sf::FloatRect bounds = fixture.widget.context_menu_bounds();
    CHECK(bounds.width == doctest::Approx(46.0f));
    const auto first = fixture.widget.context_action_at({bounds.left + 5.0f, bounds.top + 5.0f});
    REQUIRE(first);
    CHECK(*first == Verb::LOOK_AT);

    fixture.release(bounds.left + 27.0f, bounds.top + 5.0f);
    REQUIRE(fixture.intents.size() == 1);
    CHECK(fixture.intents.back().kind == RoomUiIntent::Kind::CHOOSE_CONTEXT_ACTION);
    CHECK(fixture.intents.back().verb == Verb::OPEN);
}

TEST_CASE("direct inventory keeps paging and resolves items from the visible page") {
    Fixture fixture;
    fixture.publish(true);
    fixture.release(80.0f, 60.0f);
    REQUIRE(fixture.intents.size() == 1);
    CHECK(fixture.intents.back().kind == RoomUiIntent::Kind::CHANGE_INVENTORY_PAGE);
    CHECK(fixture.intents.back().index == 1);

    fixture.state.command.inventory_page_index = 1;
    fixture.publish(true);
    const auto visible = fixture.widget.inventory_item_at({20.0f, 20.0f});
    REQUIRE(visible);
    CHECK(*visible == "map");
}
