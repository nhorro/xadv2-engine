#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/information_overlay.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/scripting.hpp"

#include <doctest/doctest.h>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <string>
#include <vector>

namespace {

class EmptySource final : public pac::core::ResourceSource {
public:
    bool exists(const std::string&) const override { return false; }
    std::string read_text(const std::string& logical) const override {
        throw pac::core::ResourceError("missing " + logical);
    }
    std::vector<std::byte> read_bytes(const std::string&) const override { return {}; }
};

} // namespace

TEST_CASE("information pages are modal and restore a previous standalone indicator") {
    pac::core::Diagnostics log(pac::core::LogLevel::ERROR);
    EmptySource source;
    pac::core::ResourceCache resources(source, log);
    pac::core::Display display({1280, 720}, {1280, 720});
    pac::core::Scripting scripting(log);
    pac::core::InformationOverlay overlay(display,
                                          resources,
                                          scripting,
                                          log,
                                          pac::core::InformationOverlayConfig{});

    overlay.show_indicator({{100.0f, 120.0f}, pac::core::IndicatorDirection::Right});
    CHECK(overlay.indicator_active());

    pac::core::InformationPage page;
    page.text = "How to play";
    page.indicator =
        pac::core::InformationIndicator{{400.0f, 300.0f}, pac::core::IndicatorDirection::Down};
    const std::string event = overlay.show(std::move(page), scripting.global_scope());
    CHECK_FALSE(event.empty());
    CHECK(overlay.modal_active());

    sf::Event move{};
    move.type = sf::Event::MouseMoved;
    CHECK(overlay.handle_event(move));
    CHECK(overlay.modal_active());

    sf::Event dismiss{};
    dismiss.type = sf::Event::KeyReleased;
    dismiss.key.code = sf::Keyboard::Enter;
    CHECK(overlay.handle_event(dismiss));
    CHECK_FALSE(overlay.modal_active());
    CHECK(overlay.indicator_active());
    CHECK_FALSE(overlay.handle_event(move));
}

TEST_CASE("a modal page can be dismissed programmatically") {
    pac::core::Diagnostics log(pac::core::LogLevel::ERROR);
    EmptySource source;
    pac::core::ResourceCache resources(source, log);
    pac::core::Display display({640, 360}, {640, 360});
    pac::core::Scripting scripting(log);
    pac::core::InformationOverlay overlay(display,
                                          resources,
                                          scripting,
                                          log,
                                          pac::core::InformationOverlayConfig{});

    overlay.show({"Message", "", "", std::nullopt}, scripting.global_scope());
    REQUIRE(overlay.modal_active());
    overlay.dismiss();
    CHECK_FALSE(overlay.modal_active());
    overlay.dismiss(); // repeated cleanup remains harmless
}
