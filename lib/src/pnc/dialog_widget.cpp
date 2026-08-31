#include "engine/pnc/dialog_widget.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/text_encoding.hpp"
#include "engine/core/text_layout.hpp"
#include "engine/pnc/data_error.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace pac::pnc {

namespace {

[[noreturn]] void fail(const std::string& code,
                       const std::string& message,
                       const YAML::Node& at = YAML::Node()) {
    pac::core::fail_at<DataError>("dialog-widget-loader", code, message, at);
}

float number(const YAML::Node& node,
             const std::string& field,
             float fallback,
             float minimum,
             float maximum) {
    if (!node) {
        return fallback;
    }
    float value = fallback;
    try {
        value = node.as<float>();
    } catch (const YAML::Exception&) {
        fail("dialog-widget.number-invalid", field + " must be a number", node);
    }
    if (value < minimum || value > maximum) {
        fail("dialog-widget.number-range", field + " is outside its valid range", node);
    }
    return value;
}

bool boolean(const YAML::Node& node, const std::string& field, bool fallback) {
    if (!node) return fallback;
    try {
        return node.as<bool>();
    } catch (const YAML::Exception&) {
        fail("dialog-widget.boolean-invalid", field + " must be true or false", node);
    }
}

WidgetAnchor anchor(const YAML::Node& node) {
    if (!node) return WidgetAnchor::BOTTOM_CENTER;
    const std::string value = node.as<std::string>();
    if (value == "top_left") return WidgetAnchor::TOP_LEFT;
    if (value == "top_center") return WidgetAnchor::TOP_CENTER;
    if (value == "top_right") return WidgetAnchor::TOP_RIGHT;
    if (value == "center_left") return WidgetAnchor::CENTER_LEFT;
    if (value == "center") return WidgetAnchor::CENTER;
    if (value == "center_right") return WidgetAnchor::CENTER_RIGHT;
    if (value == "bottom_left") return WidgetAnchor::BOTTOM_LEFT;
    if (value == "bottom_center") return WidgetAnchor::BOTTOM_CENTER;
    if (value == "bottom_right") return WidgetAnchor::BOTTOM_RIGHT;
    fail("dialog-widget.anchor-invalid", "placement.anchor is not recognized", node);
}

sf::Color color(const YAML::Node& node, const std::string& field, sf::Color fallback) {
    if (!node) {
        return fallback;
    }
    const std::string value = node.as<std::string>();
    const bool alpha = value.size() == 9;
    if ((value.size() != 7 && !alpha) || value.front() != '#') {
        fail("dialog-widget.color-invalid", field + " must be #RRGGBB or #RRGGBBAA", node);
    }
    const auto nibble = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::array<int, 8> n{};
    for (std::size_t i = 0; i < (alpha ? 8u : 6u); ++i) {
        n[i] = nibble(value[i + 1]);
        if (n[i] < 0) {
            fail("dialog-widget.color-invalid", field + " contains a non-hex digit", node);
        }
    }
    return {static_cast<sf::Uint8>(n[0] * 16 + n[1]),
            static_cast<sf::Uint8>(n[2] * 16 + n[3]),
            static_cast<sf::Uint8>(n[4] * 16 + n[5]),
            alpha ? static_cast<sf::Uint8>(n[6] * 16 + n[7]) : sf::Uint8{255}};
}

std::string asset(const YAML::Node& node,
                  const std::string& base_dir,
                  const std::string& field) {
    if (!node) {
        return {};
    }
    const std::string joined = pac::core::logical_join(base_dir, node.as<std::string>());
    if (!pac::core::is_valid_logical_path(joined)) {
        fail("dialog-widget.asset-invalid", field + " is not a valid logical path", node);
    }
    return joined;
}

void draw_arrow(sf::RenderTarget& target, sf::FloatRect rect, bool up, sf::Color color) {
    sf::ConvexShape triangle(3);
    const float inset = std::min(rect.width, rect.height) * 0.22f;
    const float left = rect.left + inset;
    const float right = rect.left + rect.width - inset;
    const float top = rect.top + inset;
    const float bottom = rect.top + rect.height - inset;
    if (up) {
        triangle.setPoint(0, {(left + right) * 0.5f, top});
        triangle.setPoint(1, {left, bottom});
        triangle.setPoint(2, {right, bottom});
    } else {
        triangle.setPoint(0, {left, top});
        triangle.setPoint(1, {right, top});
        triangle.setPoint(2, {(left + right) * 0.5f, bottom});
    }
    triangle.setFillColor(color);
    target.draw(triangle);
}

} // namespace

DialogPageLayout layout_dialog_options(const std::vector<std::string>& labels,
                                       int page_index,
                                       sf::FloatRect area,
                                       float line_height,
                                       float option_gap,
                                       float arrow_size,
                                       const std::function<float(const std::string&)>& measure) {
    DialogPageLayout out;
    if (labels.empty() || line_height <= 0.0f || area.height <= 0.0f) {
        return out;
    }
    const float gutter = arrow_size > 0.0f ? arrow_size + option_gap : 0.0f;
    const float text_width = std::max(1.0f, area.width - gutter);
    std::vector<std::vector<std::string>> wrapped;
    std::vector<float> heights;
    for (const std::string& label : labels) {
        auto lines = pac::core::wrap_text(label, text_width, measure);
        heights.push_back(static_cast<float>(lines.size()) * line_height);
        wrapped.push_back(std::move(lines));
    }
    std::vector<int> page_of(labels.size(), 0);
    int page = 0;
    float y = area.top;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (y > area.top + 0.001f && y + heights[i] > area.top + area.height) {
            ++page;
            y = area.top;
        }
        page_of[i] = page;
        y += heights[i] + option_gap;
    }
    out.page_count = page + 1;
    out.page_index = std::clamp(page_index, 0, out.page_count - 1);
    out.has_prev = out.page_index > 0;
    out.has_next = out.page_index < out.page_count - 1;
    y = area.top;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (page_of[i] != out.page_index) continue;
        out.rows.push_back({static_cast<int>(i), wrapped[i], {area.left, y, text_width, heights[i]}});
        y += heights[i] + option_gap;
    }
    if (gutter > 0.0f) {
        const float x = area.left + area.width - arrow_size;
        out.prev_arrow = {x, area.top, arrow_size, arrow_size};
        out.next_arrow = {x, area.top + area.height - arrow_size, arrow_size, arrow_size};
    }
    return out;
}

DialogWidgetConfig parse_dialog_widget_config(const std::string& yaml_text,
                                               const std::string& logical_path) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& error) {
        fail("dialog-widget.invalid-yaml", error.what());
    }
    const YAML::Node node = root["dialog_widget"];
    if (!node || !node.IsMap()) {
        fail("dialog-widget.root-missing", "root must contain dialog_widget", root);
    }
    DialogWidgetConfig config;
    const std::string base_dir = logical_path.empty() ? "" : pac::core::logical_dir(logical_path);
    if (const YAML::Node size = node["design_size"]) {
        if (!size.IsSequence() || size.size() != 2) {
            fail("dialog-widget.design-size-invalid", "design_size must be [width, height]", size);
        }
        config.design_size = {number(size[0], "design_size.width", 1280.0f, 1.0f, 100000.0f),
                              number(size[1], "design_size.height", 720.0f, 1.0f, 100000.0f)};
    }
    config.min_width = number(node["min_width"], "min_width", config.min_width, 1.0f, config.design_size.x);
    config.max_width = number(node["max_width"], "max_width", config.max_width, 1.0f, config.design_size.x);
    config.max_height = number(node["max_height"], "max_height", config.max_height, 1.0f, config.design_size.y);
    config.option_gap = number(node["option_gap"], "option_gap", config.option_gap, 0.0f, config.design_size.y);
    config.border_thickness = number(node["border_thickness"], "border_thickness", config.border_thickness, 0.0f, 20.0f);
    config.text_outline_thickness = number(node["text_outline_thickness"], "text_outline_thickness", config.text_outline_thickness, 0.0f, 20.0f);
    if (config.min_width > config.max_width) {
        fail("dialog-widget.width-invalid", "min_width must be <= max_width", node);
    }
    if (const YAML::Node placement = node["placement"]) {
        if (!placement.IsMap()) {
            fail("dialog-widget.placement-invalid", "placement must be a map", placement);
        }
        if (const YAML::Node position = placement["position"]) {
            if (!position.IsSequence() || position.size() != 2) {
                fail("dialog-widget.position-invalid", "placement.position must be [x, y]", position);
            }
            config.placement.position = {
                number(position[0], "placement.position.x", 0.5f, 0.0f, 1.0f),
                number(position[1], "placement.position.y", 1.0f, 0.0f, 1.0f)};
        }
        config.placement.anchor = anchor(placement["anchor"]);
        if (const YAML::Node offset = placement["offset"]) {
            if (!offset.IsSequence() || offset.size() != 2) {
                fail("dialog-widget.offset-invalid", "placement.offset must be [x, y]", offset);
            }
            config.placement.offset = {
                number(offset[0], "placement.offset.x", 0.0f, -config.design_size.x, config.design_size.x),
                number(offset[1], "placement.offset.y", -24.0f, -config.design_size.y, config.design_size.y)};
        }
    }
    config.opacity = number(node["opacity"], "opacity", config.opacity, 0.0f, 1.0f);
    config.transition.fade_duration = number(node["fade_duration"],
                                             "fade_duration",
                                             config.transition.fade_duration,
                                             0.0f,
                                             10.0f);
    config.transition.capture_while_hiding = boolean(node["capture_while_hiding"],
                                                     "capture_while_hiding",
                                                     config.transition.capture_while_hiding);
    if (const YAML::Node padding = node["padding"]) {
        if (!padding.IsSequence() || padding.size() != 4) {
            fail("dialog-widget.padding-invalid", "padding must be [left, top, right, bottom]", padding);
        }
        config.padding = {number(padding[0], "padding.left", 20.0f, 0.0f, config.design_size.x),
                          number(padding[1], "padding.top", 14.0f, 0.0f, config.design_size.y),
                          number(padding[2], "padding.right", 20.0f, 0.0f, config.design_size.x),
                          number(padding[3], "padding.bottom", 14.0f, 0.0f, config.design_size.y)};
    }
    config.font = asset(node["font"], base_dir, "font");
    config.font_size = static_cast<unsigned>(number(node["font_size"], "font_size", static_cast<float>(config.font_size), 1.0f, 300.0f));
    config.text = color(node["text"], "text", config.text);
    config.hover_text = color(node["hover_text"], "hover_text", config.hover_text);
    config.text_outline = color(node["text_outline"], "text_outline", config.text_outline);
    config.background = color(node["background"], "background", config.background);
    config.border = color(node["border"], "border", config.border);
    return config;
}

DialogWidget::DialogWidget(DialogWidgetConfig config,
                           sf::Vector2u runtime_size,
                           const sf::Font* font,
                           RoomUiIntentSink intent_sink)
    : config_(std::move(config)), runtime_size_(runtime_size), font_(font),
      intent_sink_(std::move(intent_sink)), presentation_(false, config_.transition) {
    presentation_.set_opacity(config_.opacity);
}

void DialogWidget::connect(RoomUiStateStream& stream) {
    state_subscription_ = stream.subscribe([this](const RoomUiState& state) {
        dialog_active_ = state.mode == RoomInteractionMode::DIALOG;
        if (dialog_active_) {
            state_ = state;
            state.widget_visible("dialog") ? presentation_.show() : presentation_.hide();
        } else {
            presentation_.hide();
        }
    });
}

InputResult DialogWidget::handle(const RoutedInput& input) {
    if (!captures(input.position)) return InputResult::PASS;
    if (input.moved()) {
        cursor_ = input.position;
        return InputResult::CONSUMED;
    }
    if (!input.primary_release()) return InputResult::CONSUMED;
    if (!dialog_active_) return InputResult::CONSUMED;
    if (state_.speech_active) {
        RoomUiIntent intent;
        intent.kind = RoomUiIntent::Kind::DISMISS_SPEECH;
        emit(std::move(intent));
        return InputResult::CONSUMED;
    }
    const DialogPageLayout layout = current_layout();
    if (layout.has_prev && layout.prev_arrow.contains(input.position)) {
        RoomUiIntent intent;
        intent.kind = RoomUiIntent::Kind::CHANGE_DIALOG_PAGE;
        intent.index = layout.page_index - 1;
        emit(std::move(intent));
    } else if (layout.has_next && layout.next_arrow.contains(input.position)) {
        RoomUiIntent intent;
        intent.kind = RoomUiIntent::Kind::CHANGE_DIALOG_PAGE;
        intent.index = layout.page_index + 1;
        emit(std::move(intent));
    } else {
        for (const DialogPageLayout::Row& row : layout.rows) {
            if (row.rect.contains(input.position)) {
                RoomUiIntent intent;
                intent.kind = RoomUiIntent::Kind::CHOOSE_DIALOG_OPTION;
                intent.index = row.option_index;
                emit(std::move(intent));
                break;
            }
        }
    }
    return InputResult::CONSUMED;
}

sf::FloatRect DialogWidget::input_bounds() const {
    return {0.0f, 0.0f, static_cast<float>(runtime_size_.x), static_cast<float>(runtime_size_.y)};
}

bool DialogWidget::captures(sf::Vector2f point) const {
    return presentation_.captures_input() && input_bounds().contains(point);
}

void DialogWidget::update(float dt) {
    if (!state_.dialog_options.empty()) {
        presentation_.set_bounds(current_layout().box);
    }
    presentation_.update(dt);
}

DialogPageLayout DialogWidget::current_layout() const {
    DialogPageLayout empty;
    if (state_.dialog_options.empty()) return empty;
    const float sx = static_cast<float>(runtime_size_.x) / config_.design_size.x;
    const float sy = static_cast<float>(runtime_size_.y) / config_.design_size.y;
    const unsigned size = std::max(1u, static_cast<unsigned>(std::lround(config_.font_size * sy)));
    const float line_height = font_ ? font_->getLineSpacing(size) : static_cast<float>(size) * 1.3f;
    const auto measure = [this, size](const std::string& value) {
        return font_ ? sf::Text(pac::core::utf8(value), *font_, size).getLocalBounds().width
                     : static_cast<float>(value.size()) * static_cast<float>(size) * 0.5f;
    };
    const float gap = config_.option_gap * sy;
    const float arrow = line_height;
    const float pad_left = config_.padding.left * sx;
    const float pad_top = config_.padding.top * sy;
    const float pad_right = config_.padding.width * sx;
    const float pad_bottom = config_.padding.height * sy;
    const float gutter = arrow + gap;
    const float min_text = std::max(1.0f, config_.min_width * sx - pad_left - pad_right);
    const float max_text =
        std::max(min_text, config_.max_width * sx - pad_left - pad_right - gutter);
    float widest = min_text;
    for (const std::string& option : state_.dialog_options) widest = std::max(widest, measure(option));
    const float text_width = std::clamp(widest, min_text, max_text);
    const float max_content_height = std::max(line_height, config_.max_height * sy - pad_top - pad_bottom);
    DialogPageLayout layout = layout_dialog_options(state_.dialog_options,
                                                     state_.dialog_page,
                                                     {0.0f, 0.0f, text_width, max_content_height},
                                                     line_height,
                                                     gap,
                                                     0.0f,
                                                     measure);
    const bool paged = layout.page_count > 1;
    const float content_width = text_width + (paged ? gutter : 0.0f);
    if (paged) {
        layout = layout_dialog_options(state_.dialog_options,
                                       state_.dialog_page,
                                       {0.0f, 0.0f, content_width, max_content_height},
                                       line_height,
                                       gap,
                                       arrow,
                                       measure);
    }
    float used_height = 0.0f;
    for (const auto& row : layout.rows) used_height = std::max(used_height, row.rect.top + row.rect.height);
    if (layout.has_prev || layout.has_next) used_height = std::max(used_height, 2.0f * arrow + gap);
    used_height = std::max(used_height, line_height);
    const float box_width = pad_left + content_width + pad_right;
    const float box_height = pad_top + used_height + pad_bottom;
    WidgetPlacement runtime_placement = config_.placement;
    runtime_placement.offset = {config_.placement.offset.x * sx, config_.placement.offset.y * sy};
    const sf::Vector2f origin = place_widget(
        {0.0f, 0.0f, static_cast<float>(runtime_size_.x), static_cast<float>(runtime_size_.y)},
        {box_width, box_height},
        runtime_placement);
    layout.box = {origin.x, origin.y, box_width, box_height};
    const float left = origin.x;
    const float top = origin.y;
    const sf::Vector2f shift{left + pad_left, top + pad_top};
    for (auto& row : layout.rows) {
        row.rect.left += shift.x;
        row.rect.top += shift.y;
    }
    if (layout.has_prev) {
        layout.prev_arrow.left += shift.x;
        layout.prev_arrow.top = shift.y;
    }
    if (layout.has_next) {
        layout.next_arrow.left += shift.x;
        layout.next_arrow.top = shift.y + used_height - arrow;
    }
    return layout;
}

void DialogWidget::draw(sf::RenderTarget& target) const {
    if (!presentation_.rendered() || state_.dialog_options.empty()) return;
    const DialogPageLayout layout = current_layout();
    surface_.draw(target, presentation_, layout.box, [this, &layout](sf::RenderTarget& surface) {
        sf::RectangleShape box({layout.box.width, layout.box.height});
        box.setPosition(layout.box.left, layout.box.top);
        box.setFillColor(config_.background);
        const float sx = static_cast<float>(runtime_size_.x) / config_.design_size.x;
        box.setOutlineThickness(config_.border_thickness * sx);
        box.setOutlineColor(config_.border);
        surface.draw(box);
        if (!font_) return;
        const float sy = static_cast<float>(runtime_size_.y) / config_.design_size.y;
        const unsigned size =
            std::max(1u, static_cast<unsigned>(std::lround(config_.font_size * sy)));
        const float line_height = font_->getLineSpacing(size);
        for (const auto& row : layout.rows) {
            const sf::Color fill = row.rect.contains(cursor_) ? config_.hover_text : config_.text;
            float y = row.rect.top;
            for (const std::string& line : row.lines) {
                sf::Text text(pac::core::utf8(line), *font_, size);
                text.setFillColor(fill);
                text.setOutlineThickness(config_.text_outline_thickness * sx);
                text.setOutlineColor(config_.text_outline);
                const auto bounds = text.getLocalBounds();
                text.setPosition(row.rect.left - bounds.left, y);
                surface.draw(text);
                y += line_height;
            }
        }
        if (layout.has_prev) {
            draw_arrow(surface,
                       layout.prev_arrow,
                       true,
                       layout.prev_arrow.contains(cursor_) ? config_.hover_text : config_.text);
        }
        if (layout.has_next) {
            draw_arrow(surface,
                       layout.next_arrow,
                       false,
                       layout.next_arrow.contains(cursor_) ? config_.hover_text : config_.text);
        }
    });
}

void DialogWidget::emit(RoomUiIntent intent) {
    if (intent_sink_) intent_sink_(intent);
}

} // namespace pac::pnc
