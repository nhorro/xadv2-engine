#include "engine/pnc/dialog_widget.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace pac::pnc;

namespace {
// Fake measurer: width == character count, so a max width reads as chars-per-line
// (mirrors the text_layout wrap_text tests). Keeps the paging logic font-independent.
float by_chars(const std::string& s) {
    return static_cast<float>(s.size());
}

// A label long enough to wrap to two lines at a 25-char text width: each half is
// 13 chars, joined by a space -> 27 > 25.
const std::string kTwoLine = "aaaaaaaaaaaaa bbbbbbbbbbbbb";
// Four 13-char words -> four lines at a 25-char width.
const std::string kFourLine = "aaaaaaaaaaaaa bbbbbbbbbbbbb ccccccccccccc ddddddddddddd";
} // namespace

TEST_CASE("layout_dialog_options: short list fits one page, no arrows") {
    const std::vector<std::string> labels{"look", "talk", "leave"};
    const DialogPageLayout l = layout_dialog_options(labels,
                                                     0,
                                                     {0.0f, 0.0f, 100.0f, 100.0f},
                                                     10.0f,
                                                     5.0f,
                                                     10.0f,
                                                     by_chars);

    CHECK(l.page_count == 1);
    CHECK(l.page_index == 0);
    CHECK_FALSE(l.has_prev);
    CHECK_FALSE(l.has_next);
    REQUIRE(l.rows.size() == 3);
    CHECK(l.rows[0].option_index == 0);
    CHECK(l.rows[2].option_index == 2);
    // Single-line options at line_height 10, gap 5: rows stack from the top.
    CHECK(l.rows[0].rect.top == doctest::Approx(0.0f));
    CHECK(l.rows[1].rect.top == doctest::Approx(15.0f));
    CHECK(l.rows[2].rect.top == doctest::Approx(30.0f));
}

TEST_CASE("layout_dialog_options: reserves a gutter and wraps at word boundaries") {
    // area width 40, arrow_size 10, gap 5 -> text width 40 - (10 + 5) = 25.
    const DialogPageLayout l = layout_dialog_options({kTwoLine},
                                                     0,
                                                     {0.0f, 0.0f, 40.0f, 200.0f},
                                                     10.0f,
                                                     5.0f,
                                                     10.0f,
                                                     by_chars);
    REQUIRE(l.rows.size() == 1);
    REQUIRE(l.rows[0].lines.size() == 2);
    CHECK(l.rows[0].lines[0] == "aaaaaaaaaaaaa"); // last word fully on its own line
    CHECK(l.rows[0].lines[1] == "bbbbbbbbbbbbb");
    // The wrapped option is two lines tall.
    CHECK(l.rows[0].rect.width == doctest::Approx(25.0f));
    CHECK(l.rows[0].rect.height == doctest::Approx(20.0f));
}

TEST_CASE("layout_dialog_options: packs whole options across pages") {
    // area height 35, line_height 10, gap 5 -> two single-line options per page.
    const std::vector<std::string> labels{"a", "b", "c", "d", "e"};
    const sf::FloatRect area{0.0f, 0.0f, 40.0f, 35.0f};

    const DialogPageLayout p0 =
        layout_dialog_options(labels, 0, area, 10.0f, 5.0f, 10.0f, by_chars);
    CHECK(p0.page_count == 3);
    CHECK(p0.page_index == 0);
    CHECK_FALSE(p0.has_prev);
    CHECK(p0.has_next);
    REQUIRE(p0.rows.size() == 2);
    CHECK(p0.rows[0].option_index == 0);
    CHECK(p0.rows[1].option_index == 1);

    const DialogPageLayout p1 =
        layout_dialog_options(labels, 1, area, 10.0f, 5.0f, 10.0f, by_chars);
    CHECK(p1.has_prev);
    CHECK(p1.has_next);
    REQUIRE(p1.rows.size() == 2);
    CHECK(p1.rows[0].option_index == 2);
    CHECK(p1.rows[1].option_index == 3);
    // Page rows always restart at the top of the area.
    CHECK(p1.rows[0].rect.top == doctest::Approx(0.0f));

    const DialogPageLayout p2 =
        layout_dialog_options(labels, 2, area, 10.0f, 5.0f, 10.0f, by_chars);
    CHECK(p2.has_prev);
    CHECK_FALSE(p2.has_next);
    REQUIRE(p2.rows.size() == 1);
    CHECK(p2.rows[0].option_index == 4);
}

TEST_CASE("layout_dialog_options: promotes a too-tall option to the next page (no clipping)") {
    // area height 30; option 0 is one line (10), option 1 wraps to two lines (20):
    // 10 + gap 5 + 20 = 35 > 30, so option 1 is promoted whole to page 2.
    const std::vector<std::string> labels{"a", kTwoLine};
    const sf::FloatRect area{0.0f, 0.0f, 40.0f, 30.0f};

    const DialogPageLayout p0 =
        layout_dialog_options(labels, 0, area, 10.0f, 5.0f, 10.0f, by_chars);
    CHECK(p0.page_count == 2);
    REQUIRE(p0.rows.size() == 1);
    CHECK(p0.rows[0].option_index == 0);

    const DialogPageLayout p1 =
        layout_dialog_options(labels, 1, area, 10.0f, 5.0f, 10.0f, by_chars);
    REQUIRE(p1.rows.size() == 1);
    CHECK(p1.rows[0].option_index == 1);
    CHECK(p1.rows[0].lines.size() == 2); // kept intact, never split across pages
}

TEST_CASE("layout_dialog_options: an option taller than a whole page still gets its own page") {
    // Page capacity ~3 lines (height 30 / line 10); a 4-line option overflows but
    // is placed alone rather than clipped.
    const std::vector<std::string> labels{"a", kFourLine};
    const sf::FloatRect area{0.0f, 0.0f, 40.0f, 30.0f};

    const DialogPageLayout p1 =
        layout_dialog_options(labels, 1, area, 10.0f, 5.0f, 10.0f, by_chars);
    CHECK(p1.page_count == 2);
    REQUIRE(p1.rows.size() == 1);
    CHECK(p1.rows[0].option_index == 1);
    CHECK(p1.rows[0].lines.size() == 4);
}

TEST_CASE("layout_dialog_options: clamps an out-of-range page index") {
    const std::vector<std::string> labels{"a", "b", "c", "d", "e"};
    const DialogPageLayout l =
        layout_dialog_options(labels, 99, {0.0f, 0.0f, 40.0f, 35.0f}, 10.0f, 5.0f, 10.0f, by_chars);
    CHECK(l.page_index == 2); // last page (3 pages total)
    REQUIRE(l.rows.size() == 1);
    CHECK(l.rows[0].option_index == 4);
}

TEST_CASE("layout_dialog_options: arrow rects sit in the right-hand gutter") {
    const std::vector<std::string> labels{"a", "b", "c", "d", "e"};
    const DialogPageLayout l =
        layout_dialog_options(labels, 1, {0.0f, 0.0f, 40.0f, 35.0f}, 10.0f, 5.0f, 10.0f, by_chars);
    // arrow_size 10 -> gutter column at x = 40 - 10 = 30.
    CHECK(l.prev_arrow.left == doctest::Approx(30.0f));
    CHECK(l.prev_arrow.top == doctest::Approx(0.0f)); // up arrow at the top
    CHECK(l.next_arrow.left == doctest::Approx(30.0f));
    CHECK(l.next_arrow.top == doctest::Approx(25.0f)); // down arrow at the bottom
}

TEST_CASE("layout_dialog_options: empty list yields no rows") {
    const DialogPageLayout l =
        layout_dialog_options({}, 0, {0.0f, 0.0f, 40.0f, 35.0f}, 10.0f, 5.0f, 10.0f, by_chars);
    CHECK(l.rows.empty());
    CHECK_FALSE(l.has_prev);
    CHECK_FALSE(l.has_next);
}

TEST_CASE("dialog widget config exposes GUI placement, material, and transition properties") {
    const DialogWidgetConfig config = parse_dialog_widget_config(R"yaml(
dialog_widget:
  design_size: [1280, 720]
  placement:
    position: [0.5, 0.5]
    anchor: center
    offset: [12, -8]
  min_width: 280
  max_width: 760
  max_height: 240
  background: "#101820CC"
  opacity: 0.9
  fade_duration: 0.35
  capture_while_hiding: false
)yaml");

    CHECK(config.placement.position == sf::Vector2f(0.5f, 0.5f));
    CHECK(config.placement.anchor == WidgetAnchor::CENTER);
    CHECK(config.placement.offset == sf::Vector2f(12.0f, -8.0f));
    CHECK(config.background.a == 204);
    CHECK(config.opacity == doctest::Approx(0.9f));
    CHECK(config.transition.fade_duration == doctest::Approx(0.35f));
    CHECK_FALSE(config.transition.capture_while_hiding);
}

TEST_CASE("widget presentation fades reversibly and keeps geometry independent") {
    WidgetPresentation presentation(false, {1.0f, true});
    presentation.set_bounds({10.0f, 20.0f, 100.0f, 40.0f});
    presentation.set_translation({4.0f, -2.0f});
    presentation.set_opacity(0.8f);

    presentation.show();
    presentation.update(0.5f);
    CHECK(presentation.visibility() == WidgetVisibility::SHOWING);
    CHECK(presentation.opacity() == doctest::Approx(0.4f));
    CHECK(presentation.bounds().left == doctest::Approx(14.0f));
    CHECK(presentation.bounds().top == doctest::Approx(18.0f));

    presentation.hide();
    presentation.update(0.25f);
    CHECK(presentation.visibility() == WidgetVisibility::HIDING);
    CHECK(presentation.captures_input());

    presentation.show();
    presentation.update(0.75f);
    CHECK(presentation.visibility() == WidgetVisibility::VISIBLE);
    CHECK(presentation.opacity() == doctest::Approx(0.8f));
}

TEST_CASE("widget placement supports future centered and radial-style components") {
    WidgetPlacement placement{{0.5f, 0.5f}, WidgetAnchor::CENTER, {10.0f, -5.0f}};
    const sf::Vector2f origin =
        place_widget({0.0f, 0.0f, 1280.0f, 720.0f}, {300.0f, 200.0f}, placement);
    CHECK(origin.x == doctest::Approx(500.0f));
    CHECK(origin.y == doctest::Approx(255.0f));
}
