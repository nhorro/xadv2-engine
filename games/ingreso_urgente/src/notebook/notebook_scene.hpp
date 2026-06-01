#pragma once

#include "engine/core/scene.hpp"

#include <SFML/Graphics/Texture.hpp>

#include <map>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sf {
class Font;
}

namespace pac::core {
struct EngineContext;
class SceneParams;
class SceneManager;
}

namespace ingreso::notebook {

class NotebookModel;
struct NotebookUiClose;

std::vector<std::string> wrapNotebookWords(const std::string& value, std::size_t max_chars);
std::string truncateNotebookText(const sf::Font* font,
                                 const std::string& value,
                                 unsigned font_size,
                                 float max_width);

struct NotebookScrollMetrics {
    float content_height = 0.0f;
    float viewport_height = 0.0f;

    float maxScroll() const;
    float clamp(float scroll) const;
    float thumbHeight(float track_height) const;
    float thumbTop(float scroll, float track_top, float track_height) const;
    float scrollFromThumbTop(float thumb_top, float track_top, float track_height) const;
};

void routeNotebookClose(const NotebookUiClose& close, pac::core::SceneManager& scenes);

class NotebookUiController {
public:
    enum class Tab { Evidence, Hypotheses };

    explicit NotebookUiController(const NotebookModel& model);
    Tab tab() const { return tab_; }
    void setTab(Tab tab) {
        tab_ = tab;
        closeClassificationSelector();
    }
    bool sectionOpen(const std::string& id) const;
    void toggleSection(const std::string& id);
    const std::string& selectedEvidence() const { return selected_evidence_; }
    void selectEvidence(std::string id) { selected_evidence_ = std::move(id); }
    std::size_t hypothesisIndex() const { return hypothesis_index_; }
    void nextHypothesis(const NotebookModel& model, int delta);
    bool classificationSelectorOpen(const std::string& subject) const {
        return open_classification_subject_ == subject;
    }
    const std::string& openClassificationSubject() const { return open_classification_subject_; }
    void toggleClassificationSelector(const std::string& subject) {
        open_classification_subject_ =
            classificationSelectorOpen(subject) ? std::string() : subject;
    }
    void closeClassificationSelector() { open_classification_subject_.clear(); }
    void requestClose() { close_requested_ = true; }
    bool closeRequested() const { return close_requested_; }

private:
    Tab tab_ = Tab::Evidence;
    std::map<std::string, bool> sections_;
    std::string selected_evidence_;
    std::size_t hypothesis_index_ = 0;
    std::string open_classification_subject_;
    bool close_requested_ = false;
};

class NotebookScene : public pac::core::Scene {
public:
    NotebookScene(pac::core::EngineContext& ctx,
                  const pac::core::SceneParams& params,
                  NotebookModel& model);
    void handle_event(const sf::Event& event) override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

private:
    void handleEvidenceClick(float x, float y);
    void handleHypothesisClick(float x, float y);
    NotebookScrollMetrics evidenceScrollMetrics() const;
    void setEvidenceScroll(float scroll);
    void handleScrollbarPress(float y);

    pac::core::EngineContext& ctx_;
    NotebookModel& model_;
    NotebookUiController ui_;
    sf::Texture background_;
    const sf::Font* font_ = nullptr;
    float evidence_scroll_ = 0.0f;
    bool dragging_scrollbar_ = false;
    bool scrollbar_pressed_ = false;
    float scrollbar_drag_offset_ = 0.0f;
    std::optional<NotebookUiController::Tab> hovered_tab_;
    bool close_dispatched_ = false;
};

} // namespace ingreso::notebook
