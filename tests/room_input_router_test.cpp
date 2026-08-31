#include "engine/pnc/inventory.hpp"
#include "engine/pnc/dialog_widget.hpp"
#include "engine/pnc/room_input_router.hpp"
#include "engine/pnc/routed_input.hpp"
#include "engine/pnc/scumm_widget.hpp"

#include <doctest/doctest.h>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Mouse.hpp>

#include <vector>

using namespace pac::pnc;

namespace {

struct ProbeLayer : RoomInputLayer {
    InputResult result = InputResult::PASS;
    int calls = 0;

    InputResult handle(const RoutedInput&) override {
        ++calls;
        return result;
    }
};

ScummPanelConfig widget_panel_config(sf::FloatRect panel = {0.0f, 0.0f, 100.0f, 100.0f}) {
    ScummPanelConfig config;
    config.layout.design_size = {100.0f, 100.0f};
    config.layout.panel_rect = panel;
    config.layout.command_bar_rect = {0.0f, 0.0f, 100.0f, 10.0f};
    config.layout.body_rect = {0.0f, 10.0f, 100.0f, 90.0f};
    config.layout.verb_panel.rect = {0.0f, 0.0f, 20.0f, 90.0f};
    config.layout.verb_panel.rows = 1;
    config.layout.verb_panel.columns = 1;
    config.layout.inventory_panel.rect = {20.0f, 0.0f, 80.0f, 90.0f};
    config.layout.inventory_panel.rows = 1;
    config.layout.inventory_panel.columns = 2;
    config.content.verbs = {Verb::OPEN};
    return config;
}

DialogWidgetConfig widget_dialog_config() {
    DialogWidgetConfig config;
    config.design_size = {100.0f, 100.0f};
    config.transition.fade_duration = 0.0f;
    return config;
}

struct WidgetFixture {
    RoomUiState state;
    RoomUiStateStream stream;
    RoomInteractionMode mode = RoomInteractionMode::COMMAND;
    bool speech = false;
    std::vector<RoomUiIntent> intents;
    ScummWidget widget;
    DialogWidget dialog;

    WidgetFixture()
        : widget(ScummPanel(widget_panel_config(), {100, 100}, nullptr, nullptr),
                 model(),
                 [this](const RoomUiIntent& intent) { intents.push_back(intent); },
                 WidgetTransition{0.0f, true}),
          dialog(widget_dialog_config(),
                 {100, 100},
                 nullptr,
                 [this](const RoomUiIntent& intent) { intents.push_back(intent); }) {
        widget.connect(stream);
        dialog.connect(stream);
        publish();
    }

    ScummWidgetModel model() { return {}; }

    void publish() {
        state.mode = mode;
        state.speech_active = speech;
        state.dialog_options = {"Ask about the archive"};
        stream.publish(state);
    }
};

} // namespace

TEST_CASE("routed pointer input is independent of platform event payloads") {
    sf::Event move{};
    move.type = sf::Event::MouseMoved;
    move.mouseMove.x = 12;
    move.mouseMove.y = 34;
    const auto routed_move = routed_pointer_input(move);
    REQUIRE(routed_move.has_value());
    CHECK(routed_move->kind == RoutedInputKind::POINTER_MOVED);
    CHECK(routed_move->position == sf::Vector2f(12.0f, 34.0f));

    sf::Event click{};
    click.type = sf::Event::MouseButtonReleased;
    click.mouseButton.button = sf::Mouse::Left;
    click.mouseButton.x = 50;
    click.mouseButton.y = 60;
    const auto routed_click = routed_pointer_input(click);
    REQUIRE(routed_click.has_value());
    CHECK(routed_click->primary_release());
    CHECK(routed_click->position == sf::Vector2f(50.0f, 60.0f));

    sf::Event key{};
    key.type = sf::Event::KeyPressed;
    CHECK_FALSE(routed_pointer_input(key).has_value());
}

TEST_CASE("room input router stops at the first consumer") {
    ProbeLayer first;
    ProbeLayer second;
    ProbeLayer third;
    second.result = InputResult::CONSUMED;
    third.result = InputResult::CONSUMED;
    RoomInputRouter router;
    router.add(first);
    router.add(second);
    router.add(third);

    CHECK(router.route({RoutedInputKind::PRIMARY_RELEASED, {10.0f, 10.0f}}) ==
          InputResult::CONSUMED);
    CHECK(first.calls == 1);
    CHECK(second.calls == 1);
    CHECK(third.calls == 0);
}

TEST_CASE("room UI state subscriptions disconnect with their owner") {
    RoomUiStateStream stream;
    RoomUiState state;
    int calls = 0;
    {
        auto subscription = stream.subscribe([&](const RoomUiState& next) {
            ++calls;
            CHECK(next.mode == RoomInteractionMode::DIALOG);
        });
        state.mode = RoomInteractionMode::DIALOG;
        stream.publish(state);
        CHECK(calls == 1);
    }
    stream.publish(state);
    CHECK(calls == 1);
}

TEST_CASE("SCUMM widget captures its rectangle before the room layer") {
    WidgetFixture fixture;
    ProbeLayer room;
    room.result = InputResult::CONSUMED;
    RoomInputRouter router;
    router.add(fixture.widget);
    router.add(room);

    CHECK(router.route({RoutedInputKind::PRIMARY_RELEASED, {5.0f, 50.0f}}) ==
          InputResult::CONSUMED);
    CHECK(room.calls == 0);
    REQUIRE(fixture.intents.size() == 1);
    CHECK(fixture.intents.back().kind == RoomUiIntent::Kind::SELECT_VERB);
    CHECK(fixture.intents.back().verb == Verb::OPEN);

    fixture.widget.set_input_enabled(false);
    CHECK(router.route({RoutedInputKind::PRIMARY_RELEASED, {5.0f, 50.0f}}) ==
          InputResult::CONSUMED);
    CHECK(room.calls == 1);

    fixture.widget.set_input_enabled(true);
    fixture.widget.set_visible(false);
    CHECK(router.route({RoutedInputKind::PRIMARY_RELEASED, {5.0f, 50.0f}}) ==
          InputResult::CONSUMED);
    CHECK(room.calls == 2);
}

TEST_CASE("independent command and dialog widgets remain modal without leaking clicks") {
    WidgetFixture fixture;
    ProbeLayer room;
    room.result = InputResult::CONSUMED;
    RoomInputRouter router;
    router.add(fixture.dialog);
    router.add(fixture.widget);
    router.add(room);

    fixture.speech = true;
    fixture.publish();
    CHECK(router.route({RoutedInputKind::PRIMARY_RELEASED, {5.0f, 50.0f}}) ==
          InputResult::CONSUMED);
    REQUIRE(fixture.intents.size() == 1);
    CHECK(fixture.intents.back().kind == RoomUiIntent::Kind::DISMISS_SPEECH);
    CHECK(room.calls == 0);

    fixture.speech = false;
    fixture.mode = RoomInteractionMode::DIALOG;
    fixture.publish();
    fixture.intents.clear();
    CHECK(router.route({RoutedInputKind::PRIMARY_RELEASED, {5.0f, 50.0f}}) ==
          InputResult::CONSUMED);
    CHECK(room.calls == 0);

    fixture.mode = RoomInteractionMode::BLOCKED;
    fixture.publish();
    CHECK(router.route({RoutedInputKind::PRIMARY_RELEASED, {5.0f, 50.0f}}) ==
          InputResult::CONSUMED);
    CHECK(room.calls == 1);
}

TEST_CASE("SCUMM widget bounds follow configured position and runtime scaling") {
    ScummWidgetModel model;
    ScummWidget widget(
        ScummPanel(widget_panel_config({10.0f, 20.0f, 50.0f, 40.0f}), {200, 200}, nullptr, nullptr),
        std::move(model),
        {});

    const sf::FloatRect bounds = widget.input_bounds();
    CHECK(bounds.left == doctest::Approx(20.0f));
    CHECK(bounds.top == doctest::Approx(40.0f));
    CHECK(bounds.width == doctest::Approx(100.0f));
    CHECK(bounds.height == doctest::Approx(80.0f));
    CHECK(widget.captures({25.0f, 45.0f}));
    CHECK_FALSE(widget.captures({5.0f, 5.0f}));

    widget.set_position({30.0f, 50.0f});
    CHECK(widget.input_bounds().left == doctest::Approx(30.0f));
    CHECK(widget.input_bounds().top == doctest::Approx(50.0f));
    CHECK(widget.captures({35.0f, 55.0f}));
    CHECK_FALSE(widget.captures({25.0f, 45.0f}));
}

TEST_CASE("SCUMM widget fades under explicit UI visibility and reverses when shown") {
    RoomUiStateStream stream;
    ScummWidget widget(
        ScummPanel(widget_panel_config(), {100, 100}, nullptr, nullptr),
        {},
        {},
        WidgetTransition{1.0f, true});
    widget.connect(stream);

    RoomUiState state;
    state.mode = RoomInteractionMode::COMMAND;
    stream.publish(state);
    CHECK(widget.visibility() == WidgetVisibility::VISIBLE);

    state.widget_visibility["scumm"] = false;
    stream.publish(state);
    CHECK(widget.visibility() == WidgetVisibility::HIDING);
    CHECK(widget.captures({5.0f, 50.0f}));
    widget.update(0.4f);

    state.widget_visibility["scumm"] = true;
    stream.publish(state);
    CHECK(widget.visibility() == WidgetVisibility::SHOWING);
    widget.update(1.0f);
    CHECK(widget.visibility() == WidgetVisibility::VISIBLE);
}
