#include "notebook/notebook_scene.hpp"

#include "notebook/notebook_model.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/display.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/text_encoding.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Window/Event.hpp>

#include <algorithm>
#include <sstream>

namespace ingreso::notebook {
namespace {

constexpr float kListRowPadding = 6.0f;
constexpr float kScrollbarWidth = 7.0f;
constexpr float kScrollbarGutter = 14.0f;
constexpr float kScrollbarMinThumbHeight = 34.0f;

bool hit(float x, float y, float left, float top, float width, float height) {
    return x >= left && x <= left + width && y >= top && y <= top + height;
}

bool hit(float x, float y, const NotebookUiRect& rect) {
    return hit(x, y, rect.x, rect.y, rect.width, rect.height);
}

sf::Color color(const NotebookUiColor& value) {
    return {value.r, value.g, value.b, value.a};
}

float listLineHeight(const NotebookUi& ui) {
    return static_cast<float>(ui.font_size) + 2.0f;
}

std::size_t charsForWidth(float width, unsigned font_size) {
    return std::max<std::size_t>(
        1, static_cast<std::size_t>(width / (static_cast<float>(font_size) * 0.56f)));
}

float scrollbarLeft(const NotebookUi& ui) {
    return ui.left_page.x + ui.left_page.width - kScrollbarWidth;
}

void text(sf::RenderTarget& target,
          const sf::Font* font,
          const std::string& value,
          float x,
          float y,
          unsigned size = 22,
          sf::Color color = sf::Color(54, 45, 37)) {
    if (!font) return;
    sf::Text drawable(pac::core::utf8(value), *font, size);
    drawable.setFillColor(color);
    drawable.setPosition(x, y);
    target.draw(drawable);
}

void button(sf::RenderTarget& target,
            const sf::Font* font,
            const std::string& label,
            float x,
            float y,
            float width,
            bool selected = false,
            unsigned font_size = 18,
            bool enabled = true) {
    sf::RectangleShape box({width, 28.0f});
    box.setPosition(x, y);
    box.setFillColor(!enabled ? sf::Color(216, 207, 184, 100)
                              : (selected ? sf::Color(214, 192, 146, 190)
                                          : sf::Color(244, 232, 196, 150)));
    box.setOutlineColor(enabled ? sf::Color(92, 72, 50) : sf::Color(120, 110, 95, 100));
    box.setOutlineThickness(1.0f);
    target.draw(box);
    text(target,
         font,
         label,
         x + 7.0f,
         y + 2.0f,
         font_size,
         enabled ? sf::Color(54, 45, 37) : sf::Color(110, 102, 91, 170));
}

std::string labelForEvidence(const NotebookModel& model, const std::string& id) {
    const Evidence* e = model.evidence(id);
    return e ? e->title : "Vacia";
}

struct EvidenceListRow {
    enum class Kind { Section, Evidence };
    Kind kind;
    std::string id;
    std::vector<std::string> lines;
    float top = 0.0f;
    float height = 0.0f;
};

struct EvidenceListLayout {
    std::vector<EvidenceListRow> rows;
    NotebookScrollMetrics scroll;
};

EvidenceListLayout evidenceListLayout(const NotebookModel& model, const NotebookUiController& ui) {
    EvidenceListLayout layout;
    const NotebookUi& style = model.definitions().ui;
    const float line_height = listLineHeight(style);
    const std::size_t title_chars =
        charsForWidth(style.left_page.width - kScrollbarGutter - 12.0f, style.font_size);
    float y = 0.0f;
    auto add = [&](EvidenceListRow::Kind kind, const std::string& id, const std::string& label) {
        std::vector<std::string> lines = wrapNotebookWords(label, title_chars);
        const float height = static_cast<float>(lines.size()) * line_height + kListRowPadding;
        layout.rows.push_back({kind, id, std::move(lines), y, height});
        y += height;
    };
    for (const auto& section : model.definitions().sections) {
        add(EvidenceListRow::Kind::Section,
            section.id,
            std::string(ui.sectionOpen(section.id) ? "[-] " : "[+] ") + section.label);
        if (!ui.sectionOpen(section.id)) continue;
        for (const auto& evidence : model.definitions().evidences) {
            if (evidence.section != section.id || !model.hasEvidence(evidence.id)) continue;
            add(EvidenceListRow::Kind::Evidence,
                evidence.id,
                std::string(evidence.id == ui.selectedEvidence() ? "> " : "  ") + evidence.title);
        }
    }
    layout.scroll = {y, style.left_page.height};
    return layout;
}

void drawScrollbar(sf::RenderTarget& target,
                   const NotebookUi& ui,
                   const NotebookScrollMetrics& metrics,
                   float scroll) {
    sf::RectangleShape track({3.0f, ui.left_page.height});
    track.setPosition(scrollbarLeft(ui) + 2.0f, ui.left_page.y);
    track.setFillColor(sf::Color(104, 83, 57, 75));
    target.draw(track);

    sf::RectangleShape thumb({kScrollbarWidth, metrics.thumbHeight(ui.left_page.height)});
    thumb.setPosition(scrollbarLeft(ui),
                      metrics.thumbTop(scroll, ui.left_page.y, ui.left_page.height));
    thumb.setFillColor(sf::Color(100, 74, 48, 150));
    thumb.setOutlineColor(sf::Color(76, 55, 38, 170));
    thumb.setOutlineThickness(1.0f);
    target.draw(thumb);
}

} // namespace

std::vector<std::string> wrapNotebookWords(const std::string& value, std::size_t width) {
    std::vector<std::string> lines;
    std::istringstream paragraphs(value);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph)) {
        if (paragraph.empty()) {
            lines.push_back("");
            continue;
        }
        std::istringstream words(paragraph);
        std::string word;
        std::string line;
        while (words >> word) {
            while (word.size() > width) {
                if (!line.empty()) {
                    lines.push_back(line);
                    line.clear();
                }
                lines.push_back(word.substr(0, width));
                word.erase(0, width);
            }
            if (!line.empty() && line.size() + 1 + word.size() > width) {
                lines.push_back(line);
                line.clear();
            }
            if (!line.empty()) line += ' ';
            line += word;
        }
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

float NotebookScrollMetrics::maxScroll() const {
    return std::max(0.0f, content_height - viewport_height);
}
float NotebookScrollMetrics::clamp(float scroll) const {
    return std::clamp(scroll, 0.0f, maxScroll());
}
float NotebookScrollMetrics::thumbHeight(float track_height) const {
    if (content_height <= 0.0f || content_height <= viewport_height) return track_height;
    return std::max(kScrollbarMinThumbHeight, track_height * viewport_height / content_height);
}
float NotebookScrollMetrics::thumbTop(float scroll, float track_top, float track_height) const {
    const float travel = track_height - thumbHeight(track_height);
    return maxScroll() <= 0.0f ? track_top : track_top + travel * clamp(scroll) / maxScroll();
}
float NotebookScrollMetrics::scrollFromThumbTop(float thumb_top, float track_top, float track_height) const {
    const float travel = track_height - thumbHeight(track_height);
    return travel <= 0.0f ? 0.0f : clamp((thumb_top - track_top) * maxScroll() / travel);
}

void routeNotebookClose(const NotebookUiClose& close, pac::core::SceneManager& scenes) {
    if (close.action == NotebookCloseAction::Goto) {
        scenes.goto_scene(close.scene);
    } else {
        scenes.pop_scene();
    }
}

namespace {

std::string markdownText(std::string value) {
    for (std::size_t pos = value.find("**"); pos != std::string::npos; pos = value.find("**", pos)) {
        value.erase(pos, 2);
    }
    return value;
}

std::string status(const NotebookModel& model, const Hypothesis& h) {
    if (model.isHypothesisComplete(h.id)) return "Completa";
    return model.countHypothesisSupports(h.id) == 0 ? "Vacia" : "Incompleta";
}

class EvidencePage {
public:
    static void draw(sf::RenderTarget& target,
                     const sf::Font* font,
                     const NotebookModel& model,
                     const NotebookUiController& ui,
                     float scroll,
                     pac::core::ResourceCache& resources) {
        const NotebookUi& style = model.definitions().ui;
        const float line_height = listLineHeight(style);
        const EvidenceListLayout layout = evidenceListLayout(model, ui);
        for (const EvidenceListRow& row : layout.rows) {
            float y = style.left_page.y + row.top - scroll;
            for (const std::string& line : row.lines) {
                if (y + line_height > style.left_page.y &&
                    y < style.left_page.y + style.left_page.height) {
                    const float indent = row.kind == EvidenceListRow::Kind::Evidence ? 12.0f : 0.0f;
                    text(target,
                         font,
                         line,
                         style.left_page.x + indent,
                         y,
                         row.kind == EvidenceListRow::Kind::Section ? style.font_size + 3
                                                                    : style.font_size);
                }
                y += line_height;
            }
        }
        drawScrollbar(target, style, layout.scroll, scroll);
        const Evidence* selected = model.evidence(ui.selectedEvidence());
        if (!selected || !model.hasEvidence(selected->id)) return;
        const unsigned title_size = style.font_size + 8;
        float title_y = style.right_page.y;
        for (const std::string& line :
             wrapNotebookWords(selected->title, charsForWidth(style.right_page.width, title_size))) {
            text(target, font, line, style.right_page.x, title_y, title_size);
            title_y += static_cast<float>(title_size) + 1.0f;
        }
        float note_y = title_y + 22.0f;
        for (const std::string& line :
             wrapNotebookWords(markdownText(selected->note),
                               charsForWidth(style.right_page.width, style.font_size))) {
            if (note_y >= style.right_page.y + style.right_page.height) break;
            text(target, font, line, style.right_page.x, note_y, style.font_size);
            note_y += listLineHeight(style);
        }
        if (!selected->image.empty()) {
            try {
                const sf::Texture& texture = resources.texture(selected->image);
                sf::Sprite sprite(texture);
                const auto size = texture.getSize();
                const float max_width = style.right_page.width * 0.55f;
                const float max_height = style.right_page.height * 0.36f;
                if (size.x && size.y) {
                    const float scale =
                        std::min(max_width / static_cast<float>(size.x),
                                 max_height / static_cast<float>(size.y));
                    sprite.setScale(scale, scale);
                    const float width = static_cast<float>(size.x) * scale;
                    const float height = static_cast<float>(size.y) * scale;
                    sprite.setPosition(style.right_page.x + (style.right_page.width - width) / 2.0f,
                                       style.right_page.y + style.right_page.height - height);
                }
                target.draw(sprite);
            } catch (const std::exception&) {
            }
        }
    }
};

class HypothesisTemplateView {
public:
    virtual ~HypothesisTemplateView() = default;
    virtual void draw(sf::RenderTarget&, const sf::Font*, const NotebookModel&, const Hypothesis&) const = 0;
};

class ClassificationTemplateView : public HypothesisTemplateView {
public:
    void draw(sf::RenderTarget& target, const sf::Font* font, const NotebookModel& model,
              const Hypothesis& h) const override {
        const NotebookUi& ui = model.definitions().ui;
        float y = ui.left_page.y + 45.0f;
        for (const auto& subject : h.subjects) {
            text(target, font, subject.label, ui.left_page.x + 50.0f, y, ui.font_size + 1);
            float x = ui.left_page.x + 245.0f;
            for (const auto& option : subject.options) {
                button(target, font, option.label, x, y, 115.0f,
                       model.hasClassification(h.id, subject.id, option.id),
                       ui.font_size > 1 ? ui.font_size - 1 : 1);
                x += 125.0f;
            }
            for (std::size_t slot = 0; slot < subject.support_slots; ++slot) {
                const std::string selected = model.classificationSupport(h.id, subject.id, slot);
                const bool enabled = model.hasClassificationCandidates(h.id, subject.id);
                button(target,
                       font,
                       "Apoyo: " + (enabled ? labelForEvidence(model, selected) : "Sin evidencias"),
                       ui.right_page.x + 105.0f,
                       y + static_cast<float>(slot) * 31.0f,
                       std::max(1.0f, ui.right_page.width - 105.0f),
                       false,
                       ui.font_size > 1 ? ui.font_size - 1 : 1,
                       enabled);
            }
            y += std::max(55.0f, 31.0f * static_cast<float>(subject.support_slots) + 8.0f);
        }
    }
};

class ArgumentTemplateView : public HypothesisTemplateView {
public:
    void draw(sf::RenderTarget& target, const sf::Font* font, const NotebookModel& model,
              const Hypothesis& h) const override {
        const NotebookUi& ui = model.definitions().ui;
        float y = ui.left_page.y + 45.0f;
        text(target, font, "Conclusion", ui.left_page.x + 50.0f, y, ui.font_size + 3);
        y += 34.0f;
        for (const auto& conclusion : h.conclusions) {
            button(target,
                   font,
                   conclusion.label,
                   ui.left_page.x + 65.0f,
                   y,
                   std::max(1.0f, ui.left_page.width - 65.0f),
                   model.hypothesisConclusion(h.id) == conclusion.id,
                   ui.font_size > 1 ? ui.font_size - 1 : 1);
            y += 34.0f;
        }
        y += 14.0f;
        for (const auto& premise : h.premises) {
            text(target,
                 font,
                 premise.label + (premise.required ? " *" : ""),
                 ui.left_page.x + 65.0f,
                 y + 3.0f,
                 ui.font_size > 1 ? ui.font_size - 1 : 1);
            const bool enabled = model.hasArgumentCandidates(h.id, premise.id);
            button(target,
                   font,
                   enabled ? labelForEvidence(model, model.hypothesisEvidence(h.id, premise.id))
                           : "Sin evidencias",
                   ui.right_page.x + 5.0f,
                   y,
                   std::max(1.0f, ui.right_page.width - 5.0f),
                   false,
                   ui.font_size > 1 ? ui.font_size - 1 : 1,
                   enabled);
            y += 34.0f;
        }
    }
};

class HypothesisPage {
public:
    static void draw(sf::RenderTarget& target, const sf::Font* font, const NotebookModel& model,
                     const NotebookUiController& ui) {
        if (model.definitions().hypotheses.empty()) return;
        const NotebookUi& style = model.definitions().ui;
        const Hypothesis& h = model.definitions().hypotheses[ui.hypothesisIndex()];
        text(target, font, "<", style.left_page.x + 5.0f, style.left_page.y - 1.0f, style.font_size + 9);
        text(target, font, h.title, style.left_page.x + 55.0f, style.left_page.y + 3.0f, style.font_size + 6);
        text(target, font, ">", style.right_page.x + style.right_page.width - 45.0f, style.right_page.y - 6.0f,
             style.font_size + 9);
        text(target, font, status(model, h), style.right_page.x + style.right_page.width - 205.0f,
             style.right_page.y, style.font_size);
        if (h.type == HypothesisTemplate::Classification) {
            ClassificationTemplateView().draw(target, font, model, h);
        } else {
            ArgumentTemplateView().draw(target, font, model, h);
        }
    }
};

} // namespace

NotebookUiController::NotebookUiController(const NotebookModel& model) {
    for (const auto& section : model.definitions().sections) sections_[section.id] = true;
    for (const auto& evidence : model.definitions().evidences) {
        if (model.hasEvidence(evidence.id)) {
            selected_evidence_ = evidence.id;
            break;
        }
    }
}
bool NotebookUiController::sectionOpen(const std::string& id) const {
    const auto it = sections_.find(id);
    return it == sections_.end() || it->second;
}
void NotebookUiController::toggleSection(const std::string& id) {
    sections_[id] = !sectionOpen(id);
}
void NotebookUiController::nextHypothesis(const NotebookModel& model, int delta) {
    const std::size_t count = model.definitions().hypotheses.size();
    if (!count) return;
    hypothesis_index_ = static_cast<std::size_t>((static_cast<int>(hypothesis_index_) + delta + static_cast<int>(count)) %
                                                 static_cast<int>(count));
}

NotebookScene::NotebookScene(pac::core::EngineContext& ctx,
                             const pac::core::SceneParams&,
                             NotebookModel& model)
    : ctx_(ctx), model_(model), ui_(model) {
    opaque_ = false;
    font_ = ctx_.resources.try_font(model_.definitions().ui.font);
    const std::vector<std::byte> bytes =
        ctx_.resources.source().read_bytes(model_.definitions().ui.background);
    sf::Image image;
    if (!image.loadFromMemory(bytes.data(), bytes.size())) {
        throw std::runtime_error("notebook: could not decode background");
    }
    image.createMaskFromColor(sf::Color::Magenta);
    if (!background_.loadFromImage(image)) {
        throw std::runtime_error("notebook: could not create keyed background texture");
    }
}

void NotebookScene::handle_event(const sf::Event& event) {
    const NotebookUi& ui = model_.definitions().ui;
    if (event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::Escape) ui_.requestClose();
    if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Right) ui_.requestClose();
    if (event.type == sf::Event::MouseWheelScrolled && ui_.tab() == NotebookUiController::Tab::Evidence) {
        setEvidenceScroll(evidence_scroll_ - event.mouseWheelScroll.delta * 45.0f);
        return;
    }
    if (event.type == sf::Event::MouseMoved) {
        const float x = static_cast<float>(event.mouseMove.x);
        const float y = static_cast<float>(event.mouseMove.y);
        hovered_tab_.reset();
        if (hit(x, y, ui.evidence_tab.hitbox)) {
            hovered_tab_ = NotebookUiController::Tab::Evidence;
        } else if (hit(x, y, ui.hypotheses_tab.hitbox)) {
            hovered_tab_ = NotebookUiController::Tab::Hypotheses;
        }
        if (!dragging_scrollbar_) return;
        const NotebookScrollMetrics metrics = evidenceScrollMetrics();
        setEvidenceScroll(metrics.scrollFromThumbTop(y - scrollbar_drag_offset_,
                                                     ui.left_page.y,
                                                     ui.left_page.height));
        return;
    }
    if (event.type == sf::Event::MouseButtonPressed &&
        event.mouseButton.button == sf::Mouse::Left &&
        ui_.tab() == NotebookUiController::Tab::Evidence &&
        hit(static_cast<float>(event.mouseButton.x),
            static_cast<float>(event.mouseButton.y),
            scrollbarLeft(ui) - 4.0f,
            ui.left_page.y,
            kScrollbarWidth + 8.0f,
            ui.left_page.height)) {
        handleScrollbarPress(static_cast<float>(event.mouseButton.y));
        return;
    }
    if (event.type != sf::Event::MouseButtonReleased || event.mouseButton.button != sf::Mouse::Left) return;
    if (scrollbar_pressed_) {
        scrollbar_pressed_ = false;
        dragging_scrollbar_ = false;
        return;
    }
    const float x = static_cast<float>(event.mouseButton.x);
    const float y = static_cast<float>(event.mouseButton.y);
    if (hit(x, y, ui.close.hitbox)) return ui_.requestClose();
    if (hit(x, y, ui.evidence_tab.hitbox)) return ui_.setTab(NotebookUiController::Tab::Evidence);
    if (hit(x, y, ui.hypotheses_tab.hitbox)) return ui_.setTab(NotebookUiController::Tab::Hypotheses);
    if (ui_.tab() == NotebookUiController::Tab::Evidence) handleEvidenceClick(x, y);
    else handleHypothesisClick(x, y);
}
void NotebookScene::update(float) {
    if (ui_.closeRequested() && !close_dispatched_) {
        close_dispatched_ = true;
        routeNotebookClose(model_.definitions().ui.close, ctx_.scenes);
    }
}
NotebookScrollMetrics NotebookScene::evidenceScrollMetrics() const {
    return evidenceListLayout(model_, ui_).scroll;
}
void NotebookScene::setEvidenceScroll(float scroll) {
    evidence_scroll_ = evidenceScrollMetrics().clamp(scroll);
}
void NotebookScene::handleScrollbarPress(float y) {
    scrollbar_pressed_ = true;
    const NotebookUi& ui = model_.definitions().ui;
    const NotebookScrollMetrics metrics = evidenceScrollMetrics();
    const float thumb_height = metrics.thumbHeight(ui.left_page.height);
    const float thumb_top = metrics.thumbTop(evidence_scroll_, ui.left_page.y, ui.left_page.height);
    if (y >= thumb_top && y <= thumb_top + thumb_height) {
        scrollbar_drag_offset_ = y - thumb_top;
    } else {
        scrollbar_drag_offset_ = thumb_height / 2.0f;
        setEvidenceScroll(metrics.scrollFromThumbTop(y - scrollbar_drag_offset_,
                                                     ui.left_page.y,
                                                     ui.left_page.height));
    }
    dragging_scrollbar_ = metrics.maxScroll() > 0.0f;
}
void NotebookScene::handleEvidenceClick(float x, float y) {
    const NotebookUiRect& page = model_.definitions().ui.left_page;
    if (!hit(x, y, page)) return;
    const EvidenceListLayout layout = evidenceListLayout(model_, ui_);
    for (const EvidenceListRow& row : layout.rows) {
        const float row_y = page.y + row.top - evidence_scroll_;
        if (!hit(x, y, page.x, row_y, page.width, row.height)) continue;
        if (row.kind == EvidenceListRow::Kind::Section) {
            ui_.toggleSection(row.id);
            setEvidenceScroll(evidence_scroll_);
            return;
        }
        ui_.selectEvidence(row.id);
        return;
    }
}
void NotebookScene::handleHypothesisClick(float x, float y) {
    const NotebookUi& ui = model_.definitions().ui;
    if (hit(x, y, ui.left_page.x - 15.0f, ui.left_page.y - 10.0f, 45.0f, 45.0f))
        return ui_.nextHypothesis(model_, -1);
    if (hit(x,
            y,
            ui.right_page.x + ui.right_page.width - 65.0f,
            ui.right_page.y - 15.0f,
            45.0f,
            45.0f))
        return ui_.nextHypothesis(model_, 1);
    if (model_.definitions().hypotheses.empty()) return;
    const Hypothesis& h = model_.definitions().hypotheses[ui_.hypothesisIndex()];
    float row_y = ui.left_page.y + 45.0f;
    if (h.type == HypothesisTemplate::Classification) {
        for (const auto& subject : h.subjects) {
            float option_x = ui.left_page.x + 245.0f;
            for (const auto& option : subject.options) {
                if (hit(x, y, option_x, row_y, 115.0f, 28.0f)) model_.setClassification(h.id, subject.id, option.id);
                option_x += 125.0f;
            }
            for (std::size_t slot = 0; slot < subject.support_slots; ++slot)
                if (model_.hasClassificationCandidates(h.id, subject.id) &&
                    hit(x,
                        y,
                        ui.right_page.x + 105.0f,
                        row_y + static_cast<float>(slot) * 31.0f,
                        std::max(1.0f, ui.right_page.width - 105.0f),
                        28.0f))
                    model_.cycleClassificationSupport(h.id, subject.id, slot);
            row_y += std::max(55.0f, 31.0f * static_cast<float>(subject.support_slots) + 8.0f);
        }
    } else {
        row_y += 34.0f;
        for (const auto& conclusion : h.conclusions) {
            if (hit(x, y, ui.left_page.x + 65.0f, row_y, std::max(1.0f, ui.left_page.width - 65.0f), 28.0f))
                model_.setConclusion(h.id, conclusion.id);
            row_y += 34.0f;
        }
        row_y += 14.0f;
        for (const auto& premise : h.premises) {
            if (model_.hasArgumentCandidates(h.id, premise.id) &&
                hit(x, y, ui.right_page.x + 5.0f, row_y, std::max(1.0f, ui.right_page.width - 5.0f), 28.0f))
                model_.cycleArgumentEvidence(h.id, premise.id);
            row_y += 34.0f;
        }
    }
}
void NotebookScene::draw(sf::RenderTarget& target) const {
    sf::Sprite bg(background_);
    const auto size = background_.getSize();
    const auto vres = ctx_.display.virtual_resolution();
    if (size.x && size.y) bg.setScale(static_cast<float>(vres.x) / size.x, static_cast<float>(vres.y) / size.y);
    target.draw(bg);
    const NotebookUi& ui = model_.definitions().ui;
    const auto tab_color = [&](NotebookUiController::Tab tab) {
        if (ui_.tab() == tab) return color(ui.tab_selected);
        if (hovered_tab_ && *hovered_tab_ == tab) return color(ui.tab_hover);
        return color(ui.tab_idle);
    };
    text(target,
         font_,
         ui.evidence_tab.label,
         ui.evidence_tab.position.x,
         ui.evidence_tab.position.y,
         ui.tab_font_size,
         tab_color(NotebookUiController::Tab::Evidence));
    text(target,
         font_,
         ui.hypotheses_tab.label,
         ui.hypotheses_tab.position.x,
         ui.hypotheses_tab.position.y,
         ui.tab_font_size,
         tab_color(NotebookUiController::Tab::Hypotheses));
    text(target,
         font_,
         ui.close.label,
         ui.close.position.x,
         ui.close.position.y,
         ui.close.font_size);
    if (ui_.tab() == NotebookUiController::Tab::Evidence)
        EvidencePage::draw(target, font_, model_, ui_, evidence_scroll_, ctx_.resources);
    else
        HypothesisPage::draw(target, font_, model_, ui_);
}

} // namespace ingreso::notebook
