#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace pac::core {
class Diagnostics;
class StateStore;
}

namespace ingreso::notebook {

struct NotebookUiPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct NotebookUiRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct NotebookUiColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct NotebookUiTab {
    std::string label;
    NotebookUiPoint position;
    NotebookUiRect hitbox;
};

enum class NotebookCloseAction { Pop, Goto };

struct NotebookUiClose {
    std::string label = "X";
    NotebookUiPoint position{1120.0f, 96.0f};
    NotebookUiRect hitbox{1110.0f, 90.0f, 45.0f, 42.0f};
    unsigned font_size = 28;
    NotebookCloseAction action = NotebookCloseAction::Pop;
    std::string scene;
};

struct NotebookUi {
    std::string background;
    std::string font;
    unsigned font_size = 19;
    NotebookUiRect left_page{125.0f, 175.0f, 400.0f, 450.0f};
    NotebookUiRect right_page{655.0f, 180.0f, 500.0f, 445.0f};
    unsigned tab_font_size = 24;
    NotebookUiColor tab_idle{92, 72, 50, 190};
    NotebookUiColor tab_hover{70, 52, 35, 255};
    NotebookUiColor tab_selected{45, 34, 25, 255};
    NotebookUiTab evidence_tab{"Evidencias", {220.0f, 116.0f}, {205.0f, 110.0f, 160.0f, 40.0f}};
    NotebookUiTab hypotheses_tab{"Hipotesis", {420.0f, 116.0f}, {405.0f, 110.0f, 190.0f, 40.0f}};
    NotebookUiClose close;
};

struct EvidenceSection {
    std::string id;
    std::string label;
};

struct Evidence {
    std::string id;
    std::string section;
    std::string title;
    std::string note;
    std::string image;
    std::set<std::string> tags;
    bool initially_discovered = false;
};

struct ClassificationOption {
    std::string id;
    std::string label;
};

struct ClassificationSubject {
    std::string id;
    std::string label;
    std::set<std::string> evidence_tags;
    std::vector<ClassificationOption> options;
    std::size_t support_slots = 1;
};

struct ArgumentConclusion {
    std::string id;
    std::string label;
};

struct ArgumentPremise {
    std::string id;
    std::string label;
    std::set<std::string> evidence_tags;
    bool required = true;
};

enum class HypothesisTemplate { Classification, Argument };

struct Hypothesis {
    std::string id;
    std::string title;
    HypothesisTemplate type = HypothesisTemplate::Classification;
    std::set<std::string> evidence_tags;
    std::vector<ClassificationSubject> subjects;
    std::vector<ArgumentConclusion> conclusions;
    std::vector<ArgumentPremise> premises;
};

struct NotebookDefinitions {
    int version = 1;
    NotebookUi ui;
    std::vector<EvidenceSection> sections;
    std::vector<Evidence> evidences;
    std::vector<Hypothesis> hypotheses;
};

NotebookDefinitions parse_notebook_yaml(const std::string& text);

class NotebookModel {
public:
    NotebookModel(NotebookDefinitions definitions,
                  pac::core::StateStore& state,
                  pac::core::Diagnostics& log);

    const NotebookDefinitions& definitions() const { return definitions_; }
    const Evidence* evidence(const std::string& id) const;
    const Hypothesis* hypothesis(const std::string& id) const;

    bool discoverEvidence(const std::string& id);
    bool hasEvidence(const std::string& id) const;
    std::size_t discoveredEvidenceCount() const;
    std::size_t discoveredEvidenceCountByTag(const std::string& tag) const;

    std::string hypothesisConclusion(const std::string& id) const;
    std::string hypothesisEvidence(const std::string& id, const std::string& premise) const;
    bool hasHypothesisEvidence(const std::string& id,
                               const std::string& premise,
                               const std::string& evidence_id) const;
    bool hasArgumentCandidates(const std::string& id, const std::string& premise) const;
    bool isHypothesisComplete(const std::string& id) const;

    bool hasClassification(const std::string& id,
                           const std::string& subject,
                           const std::string& option) const;
    bool hasClassificationSupport(const std::string& id,
                                  const std::string& subject,
                                  const std::string& evidence_id) const;
    std::string classificationSupport(const std::string& id,
                                      const std::string& subject,
                                      std::size_t slot) const;
    bool hasClassificationCandidates(const std::string& id, const std::string& subject) const;
    bool hypothesisHasTagSupport(const std::string& id, const std::string& tag) const;
    std::size_t countHypothesisSupports(const std::string& id) const;

    void setConclusion(const std::string& id, const std::string& conclusion);
    void setClassification(const std::string& id,
                           const std::string& subject,
                           const std::string& option);
    std::string cycleArgumentEvidence(const std::string& id, const std::string& premise);
    std::string cycleClassificationSupport(const std::string& id,
                                           const std::string& subject,
                                           std::size_t slot);
    void reset();

private:
    std::string stateString(const std::string& key) const;
    void setOrErase(const std::string& key, const std::string& value);
    std::vector<std::string> argumentCandidates(const Hypothesis& h, const ArgumentPremise& p) const;
    std::vector<std::string>
    classificationCandidates(const Hypothesis& h, const ClassificationSubject& subject) const;
    std::set<std::string> selectedSupports(const Hypothesis& h, const std::string& except_key) const;
    std::string cycle(const std::string& key, std::vector<std::string> candidates, const Hypothesis& h);

    NotebookDefinitions definitions_;
    pac::core::StateStore* state_;
    pac::core::Diagnostics* log_;
};

} // namespace ingreso::notebook
