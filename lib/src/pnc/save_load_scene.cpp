#include "engine/pnc/save_load_scene.hpp"

#include "engine/core/cursor.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/save_service.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/text_encoding.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Window/Event.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace pac::pnc {

namespace {

constexpr std::size_t kMaxDescriptionLen = 40;
constexpr std::size_t kMaxRowCount = pac::core::SaveService::kSlotCount;

// Layout constants in virtual pixels; centered horizontally on the screen.
constexpr float kPanelWidth = 880.0f;
constexpr float kRowHeight = 96.0f;
constexpr float kRowGap = 16.0f;
constexpr float kThumbW = 128.0f;
constexpr float kThumbH = 72.0f;
constexpr float kButtonW = 130.0f;
constexpr float kButtonH = 40.0f;
constexpr float kInputH = 32.0f;
constexpr float kInnerPad = 14.0f;

// A row's vertical band consists of the thumb (top), a label line (below the
// thumb's top), and a second line for either the description (load mode) or
// the text input (save mode).

// Format a Unix timestamp as `YYYY-MM-DD HH:MM` in local time. Returns empty
// on zero / negative input.
std::string format_unix_seconds(std::int64_t s) {
    if (s <= 0) {
        return {};
    }
    const std::time_t t = static_cast<std::time_t>(s);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf,
                  sizeof(buf),
                  "%04d-%02d-%02d %02d:%02d",
                  tm.tm_year + 1900,
                  tm.tm_mon + 1,
                  tm.tm_mday,
                  tm.tm_hour,
                  tm.tm_min);
    return buf;
}

bool contains(const sf::FloatRect& r, float x, float y) {
    return x >= r.left && x <= r.left + r.width && y >= r.top && y <= r.top + r.height;
}

} // namespace

SaveLoadScene::SaveLoadScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx), ui_sounds_(params) {
    const std::string mode = params.get_or("mode", "save");
    mode_ = (mode == "load") ? Mode::LOAD : Mode::SAVE;
    if (mode != "save" && mode != "load") {
        ctx_.log.warn("SaveLoadScene: unknown mode '" + mode +
                      "' (want save | load); defaulting to save");
    }

    background_path_ = params.get_or("background", "");
    room_scene_id_ = params.get_or("room_scene", "room_view");

    const std::string font_path = params.get_or("font", "");
    if (!font_path.empty()) {
        font_ = ctx_.resources.try_font(font_path);
    }
    if (const auto fs = params.get("font_size")) {
        try {
            font_size_ = static_cast<unsigned>(std::stoul(*fs));
        } catch (const std::exception&) {
            ctx_.log.warn("SaveLoadScene: invalid font_size '" + *fs + "'; using default");
        }
    }
    refresh_summaries();

    // In save mode, focus a sensible slot up front so the player can type a
    // description without an extra click. Prefer the first empty manual slot,
    // else fall back to manual slot 1 (the canonical "first" save). Load mode
    // doesn't need a focused row — only the per-row Load buttons act.
    if (mode_ == Mode::SAVE) {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].slot == pac::core::SaveService::kAutosaveSlot) {
                continue;
            }
            if (!rows_[i].exists) {
                focused_row_ = static_cast<int>(i);
                break;
            }
        }
        if (focused_row_ < 0) {
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                if (rows_[i].slot != pac::core::SaveService::kAutosaveSlot) {
                    focused_row_ = static_cast<int>(i);
                    break;
                }
            }
        }
    }
}

void SaveLoadScene::refresh_summaries() {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const float total_h = static_cast<float>(kMaxRowCount) * kRowHeight +
                          static_cast<float>(kMaxRowCount - 1) * kRowGap;
    const float top0 = (static_cast<float>(vres.y) - total_h) / 2.0f + 24.0f;

    rows_.clear();
    rows_.reserve(kMaxRowCount);
    for (int slot = 0; slot < static_cast<int>(kMaxRowCount); ++slot) {
        SlotView v;
        v.slot = slot;
        v.exists = ctx_.saves.slot_exists(slot);
        if (v.exists) {
            if (auto summary = ctx_.saves.slot_summary(slot)) {
                v.description = summary->description;
                v.saved_at = summary->saved_at;
            }
        }
        // Seed the input with the previous description so a re-save against the
        // same slot doesn't force re-typing.
        v.draft = sf::String::fromUtf8(v.description.begin(), v.description.end());
        // Thumbnail sidecar (#119): load from disk if present, else leave null
        // and the row falls back to the placeholder. A load failure is logged
        // by SFML but doesn't break the row.
        if (ctx_.saves.slot_has_thumbnail(slot)) {
            auto tex = std::make_unique<sf::Texture>();
            if (tex->loadFromFile(ctx_.saves.thumbnail_path(slot).string())) {
                tex->setSmooth(true);
                v.thumbnail = std::move(tex);
            }
        }
        const float row_top = top0 + static_cast<float>(slot) * (kRowHeight + kRowGap);
        compute_row_rects(row_top, v);
        rows_.push_back(std::move(v));
    }

    // Back button: centered, just below the row band.
    const float bk_w = 180.0f;
    const float bk_y = top0 + total_h + 24.0f;
    back_button_ =
        sf::FloatRect((static_cast<float>(vres.x) - bk_w) / 2.0f, bk_y, bk_w, kButtonH + 4.0f);
}

void SaveLoadScene::compute_row_rects(float top, SlotView& v) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const float panel_left = (static_cast<float>(vres.x) - kPanelWidth) / 2.0f;

    v.row = sf::FloatRect(panel_left, top, kPanelWidth, kRowHeight);

    const float input_top = top + (kRowHeight - kInputH) - kInnerPad;
    // The input fills the space between the thumb and the button. Reserved for
    // save mode + manual slots; load mode and the autosave row hide it.
    const bool manual = v.slot != pac::core::SaveService::kAutosaveSlot;
    if (mode_ == Mode::SAVE && manual) {
        const float input_left = panel_left + kThumbW + kInnerPad * 2.0f;
        const float input_w = kPanelWidth - kThumbW - kButtonW - kInnerPad * 4.0f;
        v.input_rect = sf::FloatRect(input_left, input_top, input_w, kInputH);
    } else {
        v.input_rect = sf::FloatRect();
    }

    const float btn_top = top + (kRowHeight - kButtonH) / 2.0f;
    const float btn_left = panel_left + kPanelWidth - kButtonW - kInnerPad;
    v.button = sf::FloatRect(btn_left, btn_top, kButtonW, kButtonH);
}

std::string SaveLoadScene::format_when(const SlotView& view) const {
    if (!view.exists) {
        return {};
    }
    if (view.saved_at > 0) {
        return format_unix_seconds(view.saved_at);
    }
    // Older save without `saved_at`: fall back to the file's mtime, mapped to
    // a Unix timestamp. The filesystem clock isn't comparable to system_clock
    // directly; use the C++20 std::chrono::clock_cast when available, otherwise
    // approximate via the epoch offset. clock_cast (not file_clock::to_sys) is
    // used because MSVC's file_time_type uses its own clock, not
    // std::chrono::file_clock, so the static to_sys helper does not apply there.
    std::error_code ec;
    const auto ft = std::filesystem::last_write_time(ctx_.saves.slot_path(view.slot), ec);
    if (ec) {
        return {};
    }
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(ft);
    const auto secs =
        std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
#else
    // Fallback: assume file_clock and system_clock share the system epoch on
    // this platform (true on Linux glibc; on Windows the duration counts from
    // 1601-01-01 — the C++20 helper above is preferred when present).
    const auto secs =
        std::chrono::duration_cast<std::chrono::seconds>(ft.time_since_epoch()).count();
#endif
    return format_unix_seconds(static_cast<std::int64_t>(secs));
}

void SaveLoadScene::handle_event(const sf::Event& event) {
    if (event.type == sf::Event::MouseMoved) {
        const int previous_row = hovered_row_;
        const bool previous_back = back_hovered_;
        hovered_row_ = -1;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (contains(rows_[i].row,
                         static_cast<float>(event.mouseMove.x),
                         static_cast<float>(event.mouseMove.y))) {
                hovered_row_ = static_cast<int>(i);
                break;
            }
        }
        back_hovered_ = contains(back_button_,
                                 static_cast<float>(event.mouseMove.x),
                                 static_cast<float>(event.mouseMove.y));
        if ((hovered_row_ >= 0 && hovered_row_ != previous_row) ||
            (back_hovered_ && !previous_back)) {
            ui_sounds_.selection(ctx_);
        }
        return;
    }
    if (event.type == sf::Event::MouseButtonReleased &&
        event.mouseButton.button == sf::Mouse::Left) {
        on_click(static_cast<float>(event.mouseButton.x), static_cast<float>(event.mouseButton.y));
        return;
    }
    if (event.type == sf::Event::TextEntered) {
        on_text(event.text.unicode);
        return;
    }
    if (event.type == sf::Event::KeyPressed) {
        on_key(event.key.code);
        return;
    }
}

void SaveLoadScene::on_click(float vx, float vy) {
    if (contains(back_button_, vx, vy)) {
        ui_sounds_.activate(ctx_);
        cancel();
        return;
    }
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        SlotView& v = rows_[i];
        if (!contains(v.row, vx, vy)) {
            continue;
        }
        // Action button?
        if (contains(v.button, vx, vy)) {
            if (mode_ == Mode::SAVE) {
                if (v.slot == pac::core::SaveService::kAutosaveSlot) {
                    return; // autosave isn't manually savable
                }
                ui_sounds_.activate(ctx_);
                save_into(v);
            } else {
                if (v.exists) {
                    ui_sounds_.activate(ctx_);
                    load_from(v);
                }
            }
            return;
        }
        // Otherwise click selects the row (for input focus in save mode).
        focused_row_ = static_cast<int>(i);
        ui_sounds_.activate(ctx_);
        return;
    }
}

void SaveLoadScene::on_text(sf::Uint32 codepoint) {
    if (mode_ != Mode::SAVE || focused_row_ < 0 || focused_row_ >= static_cast<int>(rows_.size())) {
        return;
    }
    SlotView& v = rows_[static_cast<std::size_t>(focused_row_)];
    if (v.slot == pac::core::SaveService::kAutosaveSlot) {
        return;
    }
    // Backspace arrives here as 8 on some platforms; we also handle it in on_key
    // for portability. Other control codepoints (tab, return, etc.) are ignored.
    if (codepoint == 8 || codepoint == 127) {
        if (!v.draft.isEmpty()) {
            v.draft.erase(v.draft.getSize() - 1, 1);
        }
        return;
    }
    if (codepoint < 0x20) {
        return;
    }
    if (v.draft.getSize() >= kMaxDescriptionLen) {
        return;
    }
    v.draft += sf::String(static_cast<sf::Uint32>(codepoint));
}

void SaveLoadScene::on_key(sf::Keyboard::Key key) {
    switch (key) {
    case sf::Keyboard::Escape:
        ui_sounds_.activate(ctx_);
        cancel();
        return;
    case sf::Keyboard::Enter:
        if (mode_ == Mode::SAVE && focused_row_ >= 0 &&
            focused_row_ < static_cast<int>(rows_.size())) {
            SlotView& v = rows_[static_cast<std::size_t>(focused_row_)];
            if (v.slot != pac::core::SaveService::kAutosaveSlot) {
                ui_sounds_.activate(ctx_);
                save_into(v);
            }
        }
        return;
    case sf::Keyboard::BackSpace:
        on_text(8u);
        return;
    case sf::Keyboard::Tab:
        if (!rows_.empty()) {
            // Cycle through manual rows (skip autosave).
            int next = focused_row_;
            for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
                next = (next + 1) % static_cast<int>(rows_.size());
                if (rows_[static_cast<std::size_t>(next)].slot !=
                    pac::core::SaveService::kAutosaveSlot) {
                    focused_row_ = next;
                    ui_sounds_.selection(ctx_);
                    break;
                }
            }
        }
        return;
    default:
        break;
    }
}

void SaveLoadScene::save_into(SlotView& view) {
    auto staged = ctx_.saves.take_pending_snap();
    if (!staged) {
        ctx_.log.error("SaveLoadScene: no staged snapshot — open the picker from the in-game menu");
        // Pop so the player isn't trapped on a screen that can't save.
        ctx_.scenes.pop_scene();
        return;
    }
    {
        const auto utf8 = view.draft.toUtf8();
        staged->description.assign(utf8.begin(), utf8.end());
    }
    // Thumbnail sidecar (#119). RoomScene's OPEN_SAVE stages the latest
    // captured image; an empty one (no capture happened yet) skips the PNG.
    const sf::Image thumb = ctx_.saves.take_pending_thumbnail();
    const sf::Image* thumb_ptr =
        (thumb.getSize().x > 0 && thumb.getSize().y > 0) ? &thumb : nullptr;
    if (!ctx_.saves.save(view.slot, *staged, thumb_ptr)) {
        // save() already logged. Re-stage so a retry against another slot works.
        ctx_.saves.stage_pending_snap(std::move(*staged));
        if (thumb_ptr) {
            ctx_.saves.stage_pending_thumbnail(thumb);
        }
        return;
    }
    ctx_.scenes.pop_scene();
}

void SaveLoadScene::load_from(const SlotView& view) {
    auto state = ctx_.saves.load(view.slot);
    if (!state) {
        return; // load() already logged
    }
    ctx_.saves.stage_restore(std::move(*state));
    // goto_scene replaces the stack (drops the picker + any room scene
    // underneath), then the room scene rebuilds and consumes the restore.
    ctx_.scenes.goto_scene(room_scene_id_);
}

void SaveLoadScene::cancel() {
    // Drop the staged snap + thumbnail on cancel so neither leaks into the
    // next time the picker is opened.
    (void) ctx_.saves.take_pending_snap();
    (void) ctx_.saves.take_pending_thumbnail();
    ctx_.scenes.pop_scene();
}

void SaveLoadScene::update(float dt) {
    (void) dt;
    if (hovered_row_ >= 0 || back_hovered_) {
        ctx_.cursor.want(pac::core::CursorKind::INTERACT);
    }
}

namespace {

void draw_thumbnail_placeholder(sf::RenderTarget& target,
                                const sf::FloatRect& dst,
                                const sf::Font* font,
                                const std::string& placeholder) {
    sf::RectangleShape box(sf::Vector2f(dst.width, dst.height));
    box.setPosition(dst.left, dst.top);
    box.setFillColor(sf::Color(18, 22, 32));
    box.setOutlineColor(sf::Color(70, 78, 95));
    box.setOutlineThickness(1.0f);
    target.draw(box);
    if (!font) {
        return;
    }
    sf::Text txt(pac::core::utf8(placeholder), *font, 14);
    txt.setFillColor(sf::Color(120, 128, 145));
    const sf::FloatRect b = txt.getLocalBounds();
    txt.setPosition(dst.left + (dst.width - b.width) / 2.0f - b.left,
                    dst.top + (dst.height - b.height) / 2.0f - b.top);
    target.draw(txt);
}

void draw_text_at(sf::RenderTarget& target,
                  const sf::Font& font,
                  const std::string& s,
                  float x,
                  float y,
                  unsigned size,
                  sf::Color color) {
    sf::Text txt(pac::core::utf8(s), font, size);
    txt.setFillColor(color);
    txt.setOutlineColor(sf::Color(0, 0, 0, 200));
    txt.setOutlineThickness(1.5f);
    txt.setPosition(x, y);
    target.draw(txt);
}

void draw_button(sf::RenderTarget& target,
                 const sf::Font* font,
                 const sf::FloatRect& rect,
                 const std::string& label,
                 bool enabled,
                 bool hot) {
    sf::RectangleShape box(sf::Vector2f(rect.width, rect.height));
    box.setPosition(rect.left, rect.top);
    if (!enabled) {
        box.setFillColor(sf::Color(24, 26, 36));
        box.setOutlineColor(sf::Color(50, 54, 70));
    } else {
        box.setFillColor(hot ? sf::Color(70, 90, 140) : sf::Color(34, 38, 54));
        box.setOutlineColor(sf::Color(90, 100, 130));
    }
    box.setOutlineThickness(1.5f);
    target.draw(box);
    if (!font) {
        return;
    }
    sf::Text txt(pac::core::utf8(label), *font, 18);
    txt.setFillColor(!enabled ? sf::Color(120, 128, 145)
                              : (hot ? sf::Color::White : sf::Color(220, 224, 235)));
    const sf::FloatRect b = txt.getLocalBounds();
    txt.setPosition(rect.left + (rect.width - b.width) / 2.0f - b.left,
                    rect.top + (rect.height - b.height) / 2.0f - b.top);
    target.draw(txt);
}

void draw_input(sf::RenderTarget& target,
                const sf::Font* font,
                const sf::FloatRect& rect,
                const sf::String& text,
                const std::string& hint,
                bool focused) {
    sf::RectangleShape box(sf::Vector2f(rect.width, rect.height));
    box.setPosition(rect.left, rect.top);
    box.setFillColor(focused ? sf::Color(18, 22, 36) : sf::Color(24, 26, 36));
    box.setOutlineColor(focused ? sf::Color(140, 160, 220) : sf::Color(60, 66, 86));
    box.setOutlineThickness(focused ? 2.0f : 1.0f);
    target.draw(box);
    if (!font) {
        return;
    }
    const bool empty = text.isEmpty();
    sf::Text txt(empty ? pac::core::utf8(hint) : text, *font, 16);
    txt.setFillColor(empty ? sf::Color(110, 116, 134) : sf::Color(230, 230, 230));
    const sf::FloatRect b = txt.getLocalBounds();
    txt.setPosition(rect.left + 10.0f - b.left,
                    rect.top + (rect.height - b.height) / 2.0f - b.top - 1.0f);
    target.draw(txt);
    if (focused && !empty) {
        sf::RectangleShape caret(sf::Vector2f(1.5f, rect.height - 10.0f));
        caret.setPosition(rect.left + 10.0f + b.width + 2.0f, rect.top + 5.0f);
        caret.setFillColor(sf::Color(230, 230, 230));
        target.draw(caret);
    }
}

} // namespace

void SaveLoadScene::draw(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const auto vw = static_cast<float>(vres.x);
    const auto vh = static_cast<float>(vres.y);

    sf::RectangleShape bg(sf::Vector2f(vw, vh));
    bg.setFillColor(sf::Color(12, 14, 22));
    target.draw(bg);

    if (!background_path_.empty()) {
        try {
            const sf::Texture& tex = ctx_.resources.texture(background_path_);
            sf::Sprite sprite(tex);
            const sf::Vector2u ts = tex.getSize();
            if (ts.x > 0 && ts.y > 0) {
                sprite.setScale(vw / static_cast<float>(ts.x), vh / static_cast<float>(ts.y));
            }
            target.draw(sprite);
        } catch (const std::exception& e) {
            ctx_.log.error(e.what());
        }
    }

    // Dim panel under the rows so the text is legible over busy backgrounds.
    const float total_h = static_cast<float>(kMaxRowCount) * kRowHeight +
                          static_cast<float>(kMaxRowCount - 1) * kRowGap;
    const float top0 = (vh - total_h) / 2.0f + 24.0f;
    sf::RectangleShape dim(sf::Vector2f(vw, vh));
    dim.setFillColor(sf::Color(0, 0, 0, 140));
    target.draw(dim);

    const pac::core::Strings& strings = ctx_.strings;

    if (font_) {
        const std::string title =
            mode_ == Mode::SAVE ? strings.ui_label("save_game") : strings.ui_label("load_game");
        sf::Text title_text(pac::core::utf8(title), *font_, font_size_ + 14u);
        title_text.setFillColor(sf::Color(255, 240, 180));
        title_text.setOutlineColor(sf::Color(0, 0, 0, 200));
        title_text.setOutlineThickness(2.0f);
        const sf::FloatRect b = title_text.getLocalBounds();
        title_text.setPosition((vw - b.width) / 2.0f - b.left, top0 - b.height - 36.0f);
        target.draw(title_text);
    }

    const std::string desc_hint = strings.ui_label("description_hint");
    const std::string empty_lbl = strings.ui_label("slot_empty");
    const std::string autosave_lbl = strings.ui_label("autosave");
    const std::string slot_lbl = strings.ui_label("slot");
    const std::string save_lbl = strings.ui_label("save_button");
    const std::string load_lbl = strings.ui_label("load_button");
    const std::string back_lbl = strings.ui_label("back");
    const std::string thumb_lbl = strings.ui_label("thumbnail_placeholder");

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const SlotView& v = rows_[i];
        const bool focused = (focused_row_ == static_cast<int>(i));
        const bool hovered = (hovered_row_ == static_cast<int>(i));
        // Row backplate
        sf::RectangleShape plate(sf::Vector2f(v.row.width, v.row.height));
        plate.setPosition(v.row.left, v.row.top);
        plate.setFillColor(focused ? sf::Color(28, 34, 54, 230)
                                   : sf::Color(20, 22, 30, hovered ? 230 : 210));
        plate.setOutlineColor(focused ? sf::Color(150, 170, 220) : sf::Color(60, 66, 86));
        plate.setOutlineThickness(focused ? 2.0f : 1.0f);
        target.draw(plate);

        // Thumbnail: the loaded sidecar texture when present (#119), else the
        // placeholder. The thumb rect is the same shape either way so layout
        // (label start, button position) stays identical.
        sf::FloatRect thumb(v.row.left + kInnerPad,
                            v.row.top + (v.row.height - kThumbH) / 2.0f,
                            kThumbW,
                            kThumbH);
        if (v.thumbnail) {
            const sf::Vector2u ts = v.thumbnail->getSize();
            sf::Sprite sprite(*v.thumbnail);
            sprite.setPosition(thumb.left, thumb.top);
            if (ts.x > 0 && ts.y > 0) {
                sprite.setScale(kThumbW / static_cast<float>(ts.x),
                                kThumbH / static_cast<float>(ts.y));
            }
            target.draw(sprite);
            // Faint outline so the image reads as a contained "card".
            sf::RectangleShape outline(sf::Vector2f(kThumbW, kThumbH));
            outline.setPosition(thumb.left, thumb.top);
            outline.setFillColor(sf::Color::Transparent);
            outline.setOutlineColor(sf::Color(70, 78, 95));
            outline.setOutlineThickness(1.0f);
            target.draw(outline);
        } else {
            draw_thumbnail_placeholder(target, thumb, font_, thumb_lbl);
        }

        // Label + description / timestamp.
        if (font_) {
            const std::string label = v.slot == pac::core::SaveService::kAutosaveSlot
                                          ? autosave_lbl
                                          : slot_lbl + " " + std::to_string(v.slot);
            draw_text_at(target,
                         *font_,
                         label,
                         thumb.left + kThumbW + kInnerPad * 2.0f,
                         v.row.top + kInnerPad,
                         font_size_,
                         sf::Color(230, 234, 245));

            std::string secondary;
            if (v.exists) {
                const std::string when = format_when(v);
                const std::string label_desc =
                    v.description.empty() ? when : (v.description + "   " + when);
                secondary = label_desc.empty() ? when : label_desc;
            } else {
                secondary = empty_lbl;
            }
            draw_text_at(target,
                         *font_,
                         secondary,
                         thumb.left + kThumbW + kInnerPad * 2.0f,
                         v.row.top + kInnerPad + static_cast<float>(font_size_) + 6.0f,
                         16,
                         v.exists ? sf::Color(190, 200, 220) : sf::Color(130, 138, 158));
        }

        // Input (save mode + manual slot).
        if (v.input_rect.width > 0.0f) {
            draw_input(target, font_, v.input_rect, v.draft, desc_hint, focused);
        }

        // Action button.
        if (mode_ == Mode::SAVE) {
            const bool enabled = v.slot != pac::core::SaveService::kAutosaveSlot;
            draw_button(target,
                        font_,
                        v.button,
                        save_lbl,
                        enabled,
                        enabled && (hovered_row_ == static_cast<int>(i)));
        } else {
            draw_button(target,
                        font_,
                        v.button,
                        load_lbl,
                        v.exists,
                        v.exists && (hovered_row_ == static_cast<int>(i)));
        }
    }

    // Back button.
    draw_button(target, font_, back_button_, back_lbl, true, back_hovered_);
}

} // namespace pac::pnc
