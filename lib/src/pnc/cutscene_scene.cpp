#include "engine/pnc/cutscene_scene.hpp"

#include "engine/core/audio.hpp"
#include "engine/core/cursor.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/strings.hpp"
#include "engine/core/text_encoding.hpp"
#include "engine/core/text_layout.hpp" // core::draw_text_block (shared text layout)

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Window/Event.hpp>

#include <algorithm>
#include <exception>
#include <string>
#include <vector>

namespace pac::pnc {

namespace {

constexpr float kHintMargin = 18.0f;        // virtual-px padding for the manual continue/skip hint
constexpr unsigned kHintTextSize = 13;      // tiny continue/skip hint (e.g. "[ENTER]")
constexpr float kTextWidthFraction = 0.86f; // slide text wraps within this fraction of the width

// Map the slide's author-facing alignment onto the shared text-layout enum.
pac::core::HAlign to_halign(CutsceneTextAlign align) {
    switch (align) {
    case CutsceneTextAlign::Left:
        return pac::core::HAlign::Left;
    case CutsceneTextAlign::Center:
        return pac::core::HAlign::Center;
    case CutsceneTextAlign::Right:
        return pac::core::HAlign::Right;
    }
    return pac::core::HAlign::Center;
}

// Place a sprite covering `target_size` at `anchor_px`, with the chosen fit.
// `contain` preserves aspect (centering the result inside `target_size`);
// `stretch` fills the box exactly. The sprite is always centered horizontally
// AND vertically on the anchor.
void anchor_image(sf::Sprite& sprite,
                  sf::Vector2u tex_size,
                  sf::Vector2f target_size,
                  sf::Vector2f anchor_px,
                  CutsceneImageFit fit) {
    if (tex_size.x == 0 || tex_size.y == 0 || target_size.x <= 0.0f || target_size.y <= 0.0f) {
        return;
    }
    const float tx = static_cast<float>(tex_size.x);
    const float ty = static_cast<float>(tex_size.y);

    float draw_w = target_size.x;
    float draw_h = target_size.y;
    if (fit == CutsceneImageFit::Contain) {
        const float scale = std::min(target_size.x / tx, target_size.y / ty);
        draw_w = tx * scale;
        draw_h = ty * scale;
    }
    sprite.setScale(draw_w / tx, draw_h / ty);
    sprite.setOrigin(tx / 2.0f, ty / 2.0f); // texture-space center
    sprite.setPosition(anchor_px);
}

} // namespace

CutsceneScene::CutsceneScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params)
    : ctx_(ctx) {
    data_path_ = params.get_or("data", "");
    on_finish_ = params.get_or("on_finish", "");
    fallback_font_path_ = params.get_or("font", "");
    if (!fallback_font_path_.empty()) {
        fallback_font_ = ctx_.resources.try_font(fallback_font_path_);
    }
}

void CutsceneScene::enter() {
    if (data_path_.empty()) {
        ctx_.log.error("Cutscene: no 'data' parameter");
        return;
    }
    try {
        data_ = parse_cutscene(ctx_.resources.read_text(data_path_));
        loaded_ = true;
    } catch (const std::exception& e) {
        ctx_.log.error(std::string("Cutscene: ") + e.what());
        return;
    }
    if (data_.slides.empty()) {
        ctx_.log.warn("Cutscene: '" + data_path_ + "' has no slides");
        finish();
        return;
    }

    // Start the track when the scene enters. Timed mode drives slide transitions
    // from `playing_offset_seconds()` (so engine stutters don't desync the
    // narration), and plays the track once; auto/manual use it as looping
    // background music.
    if (!data_.audio.empty()) {
        const bool loop = data_.mode != CutsceneAdvanceMode::Timed;
        ctx_.audio.music.play(data_.audio, loop);
        audio_playing_ = true;
    }
    apply_slide(0);
    if (data_.mode != CutsceneAdvanceMode::Timed) {
        begin_fade_in();
    }
}

void CutsceneScene::leave() {
    // `audio_persist` hands the track to the next scene (e.g. a room script that
    // stops it on entry); otherwise it is cut at the cutscene boundary.
    if (audio_playing_ && !data_.audio_persist) {
        ctx_.audio.music.stop();
        audio_playing_ = false;
    }
}

void CutsceneScene::apply_slide(std::size_t i) {
    current_ = i;
    slide_elapsed_ = 0.0f;
}

void CutsceneScene::request_advance() {
    // Only the steady Hold phase starts a transition; ignore input/timers while a
    // fade is already in flight.
    if (phase_ == Phase::Hold) {
        phase_ = Phase::FadeOut;
        phase_elapsed_ = 0.0f;
    }
}

void CutsceneScene::begin_fade_in() {
    phase_ = Phase::FadeIn;
    phase_elapsed_ = 0.0f;
}

float CutsceneScene::fade_overlay_alpha() const {
    switch (phase_) {
    case Phase::FadeIn:
        if (data_.fade.in <= 0.0f) {
            return 0.0f;
        }
        return 1.0f - std::clamp(phase_elapsed_ / data_.fade.in, 0.0f, 1.0f);
    case Phase::FadeOut:
        if (data_.fade.out <= 0.0f) {
            return 0.0f;
        }
        return std::clamp(phase_elapsed_ / data_.fade.out, 0.0f, 1.0f);
    case Phase::Hold:
        break;
    }
    return 0.0f;
}

void CutsceneScene::finish() {
    if (finished_) {
        return;
    }
    finished_ = true;
    if (audio_playing_ && !data_.audio_persist) {
        ctx_.audio.music.stop();
        audio_playing_ = false;
    }
    if (on_finish_.empty()) {
        ctx_.log.warn("Cutscene: no 'on_finish' outcome; quitting");
        ctx_.scenes.quit();
    } else {
        ctx_.scenes.goto_scene(on_finish_);
    }
}

void CutsceneScene::handle_event(const sf::Event& event) {
    if (!loaded_ || finished_) {
        return;
    }
    if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
        // Esc always skips the whole cutscene — regardless of mode.
        finish();
        return;
    }
    if (data_.mode != CutsceneAdvanceMode::Manual) {
        return;
    }
    const bool advance_key =
        event.type == sf::Event::KeyPressed &&
        (event.key.code == sf::Keyboard::Enter || event.key.code == sf::Keyboard::Space);
    const bool advance_click =
        event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left;
    if (advance_key || advance_click) {
        if (phase_ == Phase::FadeIn) {
            // Snap a slow fade-in to fully visible so the first click feels responsive.
            phase_ = Phase::Hold;
            phase_elapsed_ = 0.0f;
            slide_elapsed_ = 0.0f;
        } else {
            request_advance();
        }
    }
}

void CutsceneScene::update(float dt) {
    ctx_.cursor.want_hidden();
    if (!loaded_ || finished_) {
        return;
    }

    scene_elapsed_ += dt;
    slide_elapsed_ += dt;
    phase_elapsed_ += dt;

    // Timed mode hard-cuts on the audio/wall clock so it can't drift; it never
    // runs the fade machine (phase_ stays Hold).
    if (data_.mode == CutsceneAdvanceMode::Timed) {
        const float clock =
            audio_playing_ ? ctx_.audio.music.playing_offset_seconds() : scene_elapsed_;
        // Walk forward to the latest slide whose `at` is in the past.
        std::size_t next = current_;
        while (next + 1 < data_.slides.size() &&
               data_.slides[next + 1].at.value_or(0.0f) <= clock) {
            ++next;
        }
        if (next != current_) {
            apply_slide(next);
        }
        // Natural end: audio finishes (so the queue stays mute until the next
        // intro scene plays it). Without audio, drift to the last slide and
        // call finish when the wall-clock has run past it by `default_duration`.
        if (audio_playing_ && !ctx_.audio.music.is_playing() &&
            current_ + 1 >= data_.slides.size()) {
            finish();
        } else if (!audio_playing_ && current_ + 1 >= data_.slides.size() &&
                   slide_elapsed_ >= data_.default_duration) {
            finish();
        }
        return;
    }

    // Auto / Manual: drive the FadeIn -> Hold -> FadeOut machine.
    switch (phase_) {
    case Phase::FadeIn:
        if (phase_elapsed_ >= data_.fade.in) {
            phase_ = Phase::Hold;
            phase_elapsed_ = 0.0f;
            slide_elapsed_ = 0.0f; // count the hold only once the slide is fully visible
        }
        break;
    case Phase::Hold:
        if (data_.mode == CutsceneAdvanceMode::Auto) {
            const float dur = data_.slides[current_].duration.value_or(data_.default_duration);
            if (slide_elapsed_ >= dur) {
                request_advance();
            }
        }
        // Manual: idle until handle_event() calls request_advance().
        break;
    case Phase::FadeOut:
        if (phase_elapsed_ >= data_.fade.out) {
            if (current_ + 1 >= data_.slides.size()) {
                finish();
            } else {
                apply_slide(current_ + 1);
                begin_fade_in();
            }
        }
        break;
    }
}

const sf::Font* CutsceneScene::font_for(const CutsceneTextStyle& style) const {
    if (!style.font.empty()) {
        // try_font logs and returns nullptr on failure — the call is cached, so
        // looking it up per frame stays cheap.
        return ctx_.resources.try_font(style.font);
    }
    return fallback_font_;
}

void CutsceneScene::draw(sf::RenderTarget& target) const {
    const sf::Vector2u vres = ctx_.display.virtual_resolution();
    const auto vw = static_cast<float>(vres.x);
    const auto vh = static_cast<float>(vres.y);

    sf::RectangleShape bg(sf::Vector2f(vw, vh));
    bg.setFillColor(data_.background_color);
    target.draw(bg);

    if (!loaded_ || data_.slides.empty()) {
        return;
    }

    const CutsceneSlide& slide = data_.slides[current_];

    // Image: drawn first so text always reads above it.
    if (slide.image) {
        try {
            const sf::Texture& tex = ctx_.resources.texture(*slide.image);
            sf::Sprite sprite(tex);
            const sf::Vector2f anchor(slide.image_position.x * vw, slide.image_position.y * vh);
            const sf::Vector2f box(slide.image_size.x * vw, slide.image_size.y * vh);
            anchor_image(sprite, tex.getSize(), box, anchor, slide.image_fit);
            target.draw(sprite);
        } catch (const std::exception& e) {
            ctx_.log.error(std::string("Cutscene: ") + e.what());
        }
    }

    // Narration band: a bottom-anchored "lower third" scrim drawn over the image
    // and under the text, so narration stays readable on a full-bleed slide.
    if (slide.text_band.height > 0.0f) {
        const float bh = slide.text_band.height * vh;
        sf::RectangleShape band(sf::Vector2f(vw, bh));
        band.setPosition(0.0f, vh - bh);
        band.setFillColor(slide.text_band.color);
        target.draw(band);
    }

    // Text: word-wrapped multi-line block, centered on the anchor.
    if (slide.text) {
        if (const sf::Font* font = font_for(slide.text_style)) {
            const sf::Vector2f anchor(slide.text_position.x * vw, slide.text_position.y * vh);
            pac::core::TextStyle style;
            style.size = slide.text_style.size;
            style.color = slide.text_style.color;
            style.outline_color = slide.text_style.outline_color;
            style.outline_thickness = slide.text_style.outline_thickness;
            pac::core::draw_text_block(target,
                                       *font,
                                       *slide.text,
                                       style,
                                       anchor,
                                       vw * kTextWidthFraction,
                                       to_halign(slide.text_align),
                                       pac::core::VAnchor::Center);
        }
    }

    // Manual mode: a quiet hint at the bottom-right so the player knows what
    // to do. The string lives in the strings file so it picks up the active
    // localization — never hardcoded (R3).
    if (data_.mode == CutsceneAdvanceMode::Manual) {
        if (const sf::Font* font = fallback_font_) {
            const std::string label = ctx_.strings.ui_label("manual_continue_hint");
            sf::Text hint(pac::core::utf8(label), *font, kHintTextSize);
            hint.setFillColor(sf::Color(200, 200, 210, 220));
            const sf::FloatRect b = hint.getLocalBounds();
            hint.setPosition(vw - b.width - kHintMargin - b.left,
                             vh - b.height - kHintMargin - b.top);
            target.draw(hint);
        }
    }

    // Dip-to-black fade overlay covers everything (image, band, text, hint).
    const float fa = fade_overlay_alpha();
    if (fa > 0.0f) {
        sf::RectangleShape overlay(sf::Vector2f(vw, vh));
        sf::Color c = data_.fade.color;
        c.a = static_cast<sf::Uint8>(std::clamp(fa, 0.0f, 1.0f) * 255.0f);
        overlay.setFillColor(c);
        target.draw(overlay);
    }
}

} // namespace pac::pnc
