#include "engine/pnc/case_resolution_scene.hpp"

#include "engine/core/audio.hpp"
#include "engine/core/cursor.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/localization.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/state_store.hpp"
#include "engine/core/text_encoding.hpp"

#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Window/Event.hpp>
#include <sol/sol.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>

namespace pac::pnc {
namespace {
const sf::Color kPanel(21, 22, 23), kCell(22, 33, 43), kBorder(58, 70, 80);
const sf::Color kText(225, 209, 171), kHover(43, 183, 214), kGold(201, 152, 46);
const sf::Color kDisabled(176, 165, 138);
constexpr std::size_t kPerPage = 12;
constexpr float kHeader = 28.0f;
constexpr float kCheckWidth = 142.0f;
constexpr float kDragThreshold = 5.0f;

bool inside(geom::Point p, float x, float y, float w, float h) {
    return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
}

void centered(sf::RenderTarget& target,
              const sf::Font& font,
              const std::string& value,
              unsigned size,
              sf::Color color,
              float x,
              float y,
              float w,
              float h) {
    sf::Text text(pac::core::utf8(value), font, size);
    text.setFillColor(color);
    const sf::FloatRect b = text.getLocalBounds();
    text.setPosition(x + (w - b.width) / 2.0f - b.left, y + (h - b.height) / 2.0f - b.top - 1.0f);
    target.draw(text);
}

float distance(geom::Point a, geom::Point b) {
    return std::hypot(b.x - a.x, b.y - a.y);
}

/// Approximate a text plane inside an authored quad. SFML provides affine text
/// transforms rather than projective warping, so we use the longer pair of
/// opposite edges as the baseline and fit uniformly inside the average extents.
/// This preserves glyph proportions while following an oblique note convincingly.
void fitted_to_slot(sf::RenderTarget& target,
                    const sf::Font& font,
                    const std::string& value,
                    unsigned size,
                    sf::Color color,
                    const geom::Polygon& area) {
    if (area.size() != 4) {
        float minx = area[0].x, maxx = minx, miny = area[0].y, maxy = miny;
        for (const auto& p : area) {
            minx = std::min(minx, p.x);
            maxx = std::max(maxx, p.x);
            miny = std::min(miny, p.y);
            maxy = std::max(maxy, p.y);
        }
        centered(target, font, value, size, color, minx, miny, maxx - minx, maxy - miny);
        return;
    }

    const auto& p0 = area[0];
    const auto& p1 = area[1];
    const auto& p2 = area[2];
    const auto& p3 = area[3];
    const float pair_a = (distance(p0, p1) + distance(p3, p2)) * 0.5f;
    const float pair_b = (distance(p1, p2) + distance(p0, p3)) * 0.5f;
    float ax, ay, width, height;
    if (pair_a >= pair_b) {
        ax = (p1.x - p0.x) + (p2.x - p3.x);
        ay = (p1.y - p0.y) + (p2.y - p3.y);
        width = pair_a;
        height = pair_b;
    } else {
        ax = (p2.x - p1.x) + (p3.x - p0.x);
        ay = (p2.y - p1.y) + (p3.y - p0.y);
        width = pair_b;
        height = pair_a;
    }
    if (std::hypot(ax, ay) < 0.001f) {
        ax = 1.0f;
        ay = 0.0f;
    }

    sf::Text text(pac::core::utf8(value), font, size);
    text.setFillColor(color);
    const sf::FloatRect b = text.getLocalBounds();
    const float available_w = std::max(1.0f, width - 16.0f);
    const float available_h = std::max(1.0f, height - 8.0f);
    const float scale = std::min(
        {available_w / std::max(1.0f, b.width), available_h / std::max(1.0f, b.height), 1.35f});
    text.setOrigin(b.left + b.width * 0.5f, b.top + b.height * 0.5f);
    text.setScale(scale, scale);
    text.setRotation(std::atan2(ay, ax) * 180.0f / 3.14159265358979323846f);
    text.setPosition((p0.x + p1.x + p2.x + p3.x) * 0.25f, (p0.y + p1.y + p2.y + p3.y) * 0.25f);
    target.draw(text);
}
} // namespace

CaseResolutionScene::CaseResolutionScene(pac::core::EngineContext& ctx,
                                         const pac::core::SceneParams& params)
    : ctx_(ctx) {
    data_path_ = params.get_or("data", "");
    terms_path_ = params.get_or("terms", "");
    logic_path_ = params.get_or("logic", "");
    on_exit_ = params.get_or("on_exit", "");
    on_solve_ = params.get_or("on_solve", "");
    const std::string font = params.get_or("font", "");
    if (!font.empty())
        font_ = ctx.resources.try_font(font);
}

void CaseResolutionScene::enter() {
    if (data_path_.empty() || terms_path_.empty()) {
        ctx_.log.error("CaseResolution: 'data' and 'terms' parameters are required");
        return;
    }
    try {
        data_ = parse_case_resolution(ctx_.resources.read_text(data_path_), {}, data_path_);
        bank_ = parse_case_terms(ctx_.resources.read_text(terms_path_));
        std::erase_if(bank_.terms, [this](const CaseTerm& term) {
            return !ctx_.state.has("__case_term." + term.id);
        });
        for (const CaseSlot& slot : data_.slots) {
            const auto saved = ctx_.state.get("__case_assignment." + data_.id + "." + slot.id);
            if (!saved || !std::holds_alternative<std::string>(*saved))
                continue;
            if (const CaseTerm* term = bank_.find(std::get<std::string>(*saved))) {
                assignments_.assign(slot, *term);
            }
        }
        loaded_ = true;
    } catch (const std::exception& e) {
        ctx_.log.error(std::string("CaseResolution: ") + e.what());
    }
    case_scope_ = ctx_.scripting.open_scope();
    if (!logic_path_.empty()) {
        try {
            runtime_.load(ctx_.scripting,
                          ctx_.resources.read_text(logic_path_),
                          logic_path_,
                          ctx_.log);
        } catch (const std::exception& e) {
            ctx_.log.error(std::string("CaseResolution: logic '") + logic_path_ + "': " + e.what());
        }
    }
    // A hook may decide to close after receiving 0 invalid slots.
    ctx_.scripting.lua().set_function("close_case_resolution", [this]() { exit(); });
}

void CaseResolutionScene::leave() {
    ctx_.scripting.lua()["close_case_resolution"] = sol::lua_nil;
    ctx_.scripting.cancel_scope(case_scope_);
}

void CaseResolutionScene::exit() {
    if (!exit_hook_run_) {
        exit_hook_run_ = true;
        ctx_.scripting.set_current_scope(case_scope_);
        runtime_.run_on_exit(exit_status_);
        ctx_.scripting.set_current_scope(ctx_.scripting.global_scope());
    }
    if (on_exit_.empty())
        ctx_.scenes.pop_scene();
    else
        ctx_.scenes.goto_scene(on_exit_);
}

std::size_t CaseResolutionScene::page_count() const {
    return std::max<std::size_t>(1, (bank_.terms.size() + kPerPage - 1) / kPerPage);
}

std::optional<std::size_t> CaseResolutionScene::term_index_at(geom::Point p) const {
    const float vw = static_cast<float>(ctx_.display.virtual_resolution().x);
    const float vh = static_cast<float>(ctx_.display.virtual_resolution().y);
    const float panel_y = data_.canvas_height;
    const float usable = vw - kCheckWidth;
    if (p.x < 0.0f || p.x >= usable || p.y < panel_y + kHeader || p.y >= vh)
        return std::nullopt;
    const float cell_w = usable / 6.0f;
    const float cell_h = (vh - panel_y - kHeader) / 2.0f;
    const std::size_t col = std::min<std::size_t>(5, static_cast<std::size_t>(p.x / cell_w));
    const std::size_t row =
        std::min<std::size_t>(1, static_cast<std::size_t>((p.y - panel_y - kHeader) / cell_h));
    const std::size_t index = page_ * kPerPage + row * 6 + col;
    return index < bank_.terms.size() ? std::optional<std::size_t>(index) : std::nullopt;
}

void CaseResolutionScene::play_sound(const std::string& path) {
    if (!path.empty())
        ctx_.audio.sfx.play(path);
}

void CaseResolutionScene::clear_assignment_for(const std::string& term_id) {
    for (const CaseSlot& slot : data_.slots) {
        const std::string* assigned = assignments_.term_for(slot.id);
        if (assigned && *assigned == term_id)
            assignments_.clear(slot.id);
        const std::string key = "__case_assignment." + data_.id + "." + slot.id;
        const auto persisted = ctx_.state.get(key);
        if (persisted && std::holds_alternative<std::string>(*persisted) &&
            std::get<std::string>(*persisted) == term_id) {
            ctx_.state.erase(key);
        }
    }
}

void CaseResolutionScene::pick_up(const std::string& term_id) {
    clear_assignment_for(term_id);
    held_term_ = term_id;
    play_sound(data_.sounds.pickup);
}

bool CaseResolutionScene::pick_up_at(geom::Point p) {
    if (!held_term_.empty())
        return false;
    if (p.y < data_.canvas_height) {
        if (const CaseSlot* slot = data_.slot_at(p)) {
            if (const std::string* term_id = assignments_.term_for(slot->id)) {
                const std::string id = *term_id;
                pick_up(id);
                return true;
            }
        }
        return false;
    }
    if (const std::optional<std::size_t> index = term_index_at(p)) {
        pick_up(bank_.terms[*index].id);
        return true;
    }
    return false;
}

void CaseResolutionScene::drop_at(geom::Point p) {
    if (held_term_.empty())
        return;
    const CaseTerm* term = bank_.find(held_term_);
    const CaseSlot* slot = p.y < data_.canvas_height ? data_.slot_at(p) : nullptr;
    if (term && slot && assignments_.assign(*slot, *term)) {
        ctx_.state.set("__case_assignment." + data_.id + "." + slot->id, term->id);
        play_sound(data_.sounds.place);
    } else {
        if (slot) {
            feedback_success_ = false;
            feedback_left_ = 1.2f;
        }
        play_sound(data_.sounds.return_to_bank);
    }
    held_term_.clear();
}

void CaseResolutionScene::activate_control(geom::Point p) {
    const float vw = static_cast<float>(ctx_.display.virtual_resolution().x);
    const float vh = static_cast<float>(ctx_.display.virtual_resolution().y);
    const float panel_y = data_.canvas_height;
    if (inside(p, vw - kCheckWidth, panel_y + kHeader, kCheckWidth, vh - panel_y - kHeader)) {
        if (!assignments_.complete(data_))
            return;
        const std::size_t invalid = assignments_.invalid_count(data_);
        exit_status_ = invalid == 0 ? "solved" : "incorrect";
        feedback_success_ = invalid == 0;
        feedback_left_ = 1.5f;
        ctx_.scripting.set_current_scope(case_scope_);
        runtime_.run_on_check(invalid);
        ctx_.scripting.set_current_scope(ctx_.scripting.global_scope());
        if (feedback_success_ && !on_solve_.empty())
            ctx_.scenes.goto_scene(on_solve_);
        return;
    }
    if (inside(p, vw - 132.0f, panel_y, 36.0f, kHeader) && page_ > 0) {
        --page_;
        return;
    }
    if (inside(p, vw - 40.0f, panel_y, 36.0f, kHeader) && page_ + 1 < page_count()) {
        ++page_;
        return;
    }
}

bool CaseResolutionScene::pointer_is_actionable(geom::Point p) const {
    const float vw = static_cast<float>(ctx_.display.virtual_resolution().x);
    const float vh = static_cast<float>(ctx_.display.virtual_resolution().y);
    const float panel_y = data_.canvas_height;
    if (!held_term_.empty()) {
        const CaseSlot* slot = p.y < panel_y ? data_.slot_at(p) : nullptr;
        const CaseTerm* term = bank_.find(held_term_);
        return slot && term && case_slot_accepts(*slot, *term);
    }
    if (p.y < panel_y) {
        const CaseSlot* slot = data_.slot_at(p);
        return slot && assignments_.term_for(slot->id);
    }
    if (term_index_at(p))
        return true;
    if (assignments_.complete(data_) &&
        inside(p, vw - kCheckWidth, panel_y + kHeader, kCheckWidth, vh - panel_y - kHeader))
        return true;
    if (page_ > 0 && inside(p, vw - 132.0f, panel_y, 36.0f, kHeader))
        return true;
    return page_ + 1 < page_count() && inside(p, vw - 40.0f, panel_y, 36.0f, kHeader);
}

void CaseResolutionScene::handle_event(const sf::Event& event) {
    if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
        exit();
        return;
    }
    if (event.type == sf::Event::MouseMoved)
        mouse_ = {float(event.mouseMove.x), float(event.mouseMove.y)};
    if (event.type == sf::Event::MouseButtonPressed &&
        event.mouseButton.button == sf::Mouse::Left && loaded_) {
        mouse_ = {float(event.mouseButton.x), float(event.mouseButton.y)};
        press_point_ = mouse_;
        left_pressed_ = true;
        picked_up_on_press_ = pick_up_at(mouse_);
    }
    if (event.type == sf::Event::MouseButtonReleased) {
        mouse_ = {float(event.mouseButton.x), float(event.mouseButton.y)};
        if (event.mouseButton.button == sf::Mouse::Right) {
            exit();
            return;
        }
        if (event.mouseButton.button == sf::Mouse::Left && loaded_ && left_pressed_) {
            const bool dragged = distance(press_point_, mouse_) >= kDragThreshold;
            if (!picked_up_on_press_ || dragged) {
                if (!held_term_.empty())
                    drop_at(mouse_);
                else
                    activate_control(mouse_);
            }
            left_pressed_ = false;
            picked_up_on_press_ = false;
        }
    }
}

void CaseResolutionScene::update(float dt) {
    feedback_left_ = std::max(0.0f, feedback_left_ - dt);
    if (loaded_ && pointer_is_actionable(mouse_))
        ctx_.cursor.want(pac::core::CursorKind::INTERACT);
}

void CaseResolutionScene::draw(sf::RenderTarget& target) const {
    const auto term_name = [this](const CaseTerm& term) {
        return ctx_.localization.text("case.term." + term.id + ".name", term.name);
    };
    const auto res = ctx_.display.virtual_resolution();
    const float vw = float(res.x), vh = float(res.y), py = data_.canvas_height;
    sf::RectangleShape fill({vw, vh});
    fill.setFillColor(data_.background_color);
    target.draw(fill);
    if (loaded_ && !data_.background.empty())
        try {
            const sf::Texture& texture = ctx_.resources.texture(data_.background);
            sf::Sprite image(texture);
            const auto size = texture.getSize();
            if (size.x && size.y)
                image.setScale(vw / float(size.x), py / float(size.y));
            target.draw(image);
        } catch (const std::exception& e) {
            ctx_.log.error(e.what());
        }

    for (const CaseSlot& slot : data_.slots) {
        sf::ConvexShape shape(slot.area.size());
        for (std::size_t i = 0; i < slot.area.size(); ++i)
            shape.setPoint(i, {slot.area[i].x, slot.area[i].y});
        const bool hovered = data_.slot_at(mouse_) == &slot;
        const sf::Color slot_tag = slot.accepts.size() == 1
                                       ? case_term_color(slot.accepts.front(), 150)
                                       : sf::Color(22, 33, 43, 190);
        shape.setFillColor(slot_tag);
        shape.setOutlineColor(
            hovered ? kHover
                    : (slot.accepts.size() == 1 ? case_term_color(slot.accepts.front()) : kGold));
        shape.setOutlineThickness(2.0f);
        target.draw(shape);
        if (font_) {
            const std::string* id = assignments_.term_for(slot.id);
            const CaseTerm* term = id ? bank_.find(*id) : nullptr;
            fitted_to_slot(target,
                           *font_,
                           term ? term_name(*term) : "…",
                           19,
                           term ? kText : kDisabled,
                           slot.area);
        }
    }

    sf::RectangleShape panel({vw, vh - py});
    panel.setPosition(0, py);
    panel.setFillColor(kPanel);
    panel.setOutlineColor(kBorder);
    panel.setOutlineThickness(-2);
    target.draw(panel);
    if (!font_)
        return;
    sf::Text title(pac::core::utf8(ctx_.strings.ui_label("case_terms_title")), *font_, 18);
    title.setFillColor(kText);
    title.setPosition(12, py + 2);
    target.draw(title);
    centered(target, *font_, "‹", 22, page_ > 0 ? kText : kDisabled, vw - 132, py, 36, kHeader);
    centered(target,
             *font_,
             std::to_string(page_ + 1) + " / " + std::to_string(page_count()),
             16,
             kText,
             vw - 96,
             py,
             56,
             kHeader);
    centered(target,
             *font_,
             "›",
             22,
             page_ + 1 < page_count() ? kText : kDisabled,
             vw - 40,
             py,
             36,
             kHeader);
    const float usable = vw - kCheckWidth, cw = usable / 6, ch = (vh - py - kHeader) / 2;
    for (std::size_t local = 0; local < kPerPage; ++local) {
        const std::size_t index = page_ * kPerPage + local;
        if (index >= bank_.terms.size())
            break;
        const float x = (local % 6) * cw, y = py + kHeader + (local / 6) * ch;
        sf::RectangleShape cell({cw, ch});
        cell.setPosition(x, y);
        cell.setFillColor(case_term_color(bank_.terms[index].tag, 135));
        const bool selected = held_term_ == bank_.terms[index].id,
                   hover = inside(mouse_, x, y, cw, ch);
        cell.setOutlineColor(selected ? kGold : (hover ? kHover : kBorder));
        cell.setOutlineThickness(-2);
        target.draw(cell);
        centered(target,
                 *font_,
                 term_name(bank_.terms[index]),
                 17,
                 selected ? kGold : (hover ? kHover : kText),
                 x,
                 y,
                 cw,
                 ch);
    }
    const bool complete = assignments_.complete(data_);
    const bool check_hover =
        complete && inside(mouse_, vw - kCheckWidth, py + kHeader, kCheckWidth, vh - py - kHeader);
    sf::RectangleShape check({kCheckWidth, vh - py - kHeader});
    check.setPosition(vw - kCheckWidth, py + kHeader);
    check.setFillColor(complete ? kCell : kPanel);
    check.setOutlineColor(feedback_left_ > 0 ? (feedback_success_ ? kGold : sf::Color(190, 70, 60))
                                             : (check_hover ? kHover : kBorder));
    check.setOutlineThickness(-2);
    target.draw(check);
    const sf::Color check_text =
        !complete ? kDisabled
                  : (feedback_left_ > 0 ? (feedback_success_ ? kGold : sf::Color(220, 100, 90))
                                        : (check_hover ? kHover : kText));
    centered(target,
             *font_,
             feedback_left_ > 0 ? (feedback_success_ ? ctx_.strings.ui_label("case_resolved")
                                                     : ctx_.strings.ui_label("case_review"))
                                : ctx_.strings.ui_label("case_check"),
             17,
             check_text,
             vw - kCheckWidth,
             py + kHeader,
             kCheckWidth,
             vh - py - kHeader);

    if (!held_term_.empty()) {
        if (const CaseTerm* term = bank_.find(held_term_)) {
            const std::string localized_name = term_name(*term);
            sf::Text label(pac::core::utf8(localized_name), *font_, 17);
            const sf::FloatRect bounds = label.getLocalBounds();
            const float width = std::clamp(bounds.width + 28.0f, 112.0f, 280.0f);
            const float height = 38.0f;
            const float x = std::clamp(mouse_.x + 18.0f, 4.0f, vw - width - 4.0f);
            const float y = std::clamp(mouse_.y + 18.0f, 4.0f, vh - height - 4.0f);
            sf::RectangleShape chip({width, height});
            chip.setPosition(x, y);
            chip.setFillColor(case_term_color(term->tag, 235));
            chip.setOutlineColor(kGold);
            chip.setOutlineThickness(2.0f);
            target.draw(chip);
            centered(target, *font_, localized_name, 17, kText, x, y, width, height);
        }
    }
}
} // namespace pac::pnc
