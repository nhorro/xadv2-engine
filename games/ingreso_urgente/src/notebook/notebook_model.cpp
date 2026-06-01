#include "notebook/notebook_model.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/load_error.hpp"
#include "engine/core/state_store.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ingreso::notebook {
namespace {

[[noreturn]] void fail(const std::string& code, const std::string& message) {
    pac::core::fail("ingreso-notebook-loader", code, message);
}

std::string required(const YAML::Node& node, const char* key, const std::string& where) {
    if (!node[key] || !node[key].IsScalar()) {
        fail("notebook.missing-field", where + " needs scalar '" + key + "'");
    }
    return node[key].as<std::string>();
}

float optionalFloat(const YAML::Node& node, const char* key, float fallback) {
    return node && node[key] ? node[key].as<float>() : fallback;
}

unsigned optionalUnsigned(const YAML::Node& node, const char* key, unsigned fallback) {
    return node && node[key] ? node[key].as<unsigned>() : fallback;
}

NotebookUiRect rect(const YAML::Node& node, NotebookUiRect fallback, const std::string& where) {
    if (!node) return fallback;
    fallback.x = optionalFloat(node, "x", fallback.x);
    fallback.y = optionalFloat(node, "y", fallback.y);
    fallback.width = optionalFloat(node, "width", fallback.width);
    fallback.height = optionalFloat(node, "height", fallback.height);
    if (fallback.width <= 0.0f || fallback.height <= 0.0f) {
        fail("notebook.invalid-ui-rect", where + " width and height must be positive");
    }
    return fallback;
}

NotebookUiPoint point(const YAML::Node& node, NotebookUiPoint fallback) {
    if (!node) return fallback;
    fallback.x = optionalFloat(node, "x", fallback.x);
    fallback.y = optionalFloat(node, "y", fallback.y);
    return fallback;
}

NotebookUiColor color(const YAML::Node& node, NotebookUiColor fallback, const std::string& where) {
    if (!node) return fallback;
    if (!node.IsSequence() || (node.size() != 3 && node.size() != 4)) {
        fail("notebook.invalid-ui-color", where + " must be [r, g, b] or [r, g, b, a]");
    }
    const auto component = [&](std::size_t index) {
        const int value = node[index].as<int>();
        if (value < 0 || value > 255) {
            fail("notebook.invalid-ui-color", where + " components must be between 0 and 255");
        }
        return static_cast<std::uint8_t>(value);
    };
    fallback.r = component(0);
    fallback.g = component(1);
    fallback.b = component(2);
    fallback.a = node.size() == 4 ? component(3) : 255;
    return fallback;
}

NotebookUiTab tab(const YAML::Node& node, NotebookUiTab fallback, const std::string& where) {
    if (!node) return fallback;
    if (node["label"]) fallback.label = node["label"].as<std::string>();
    fallback.position = point(node["position"], fallback.position);
    fallback.hitbox = rect(node["hitbox"], fallback.hitbox, where + ".hitbox");
    return fallback;
}

NotebookUiClose closeControl(const YAML::Node& node, NotebookUiClose fallback) {
    if (!node) return fallback;
    if (node["label"]) fallback.label = node["label"].as<std::string>();
    fallback.position = point(node["position"], fallback.position);
    fallback.hitbox = rect(node["hitbox"], fallback.hitbox, "ui.close.hitbox");
    fallback.font_size = optionalUnsigned(node, "font_size", fallback.font_size);
    if (fallback.font_size == 0) {
        fail("notebook.invalid-font-size", "ui.close.font_size must be positive");
    }
    const std::string action = node["action"] ? node["action"].as<std::string>() : "pop";
    if (action == "pop") {
        fallback.action = NotebookCloseAction::Pop;
        fallback.scene.clear();
    } else if (action == "goto") {
        fallback.action = NotebookCloseAction::Goto;
        fallback.scene = required(node, "scene", "ui.close");
    } else {
        fail("notebook.invalid-close-action", "ui.close.action must be 'pop' or 'goto'");
    }
    return fallback;
}

std::set<std::string> tags(const YAML::Node& node) {
    std::set<std::string> out;
    if (!node) {
        return out;
    }
    if (!node.IsSequence()) {
        fail("notebook.invalid-tags", "tags must be a sequence");
    }
    for (const YAML::Node& tag : node) {
        out.insert(tag.as<std::string>());
    }
    return out;
}

bool intersects(const std::set<std::string>& a, const std::set<std::string>& b) {
    if (b.empty()) {
        return true;
    }
    for (const std::string& value : b) {
        if (a.count(value) != 0) {
            return true;
        }
    }
    return false;
}

std::string evidenceKey(const std::string& id) {
    return "notebook.evidence." + id + ".discovered";
}
std::string conclusionKey(const std::string& id) {
    return "notebook.hypothesis." + id + ".conclusion";
}
std::string premiseKey(const std::string& id, const std::string& premise) {
    return "notebook.hypothesis." + id + ".premise." + premise + ".evidence";
}
std::string subjectOptionKey(const std::string& id, const std::string& subject) {
    return "notebook.hypothesis." + id + ".subject." + subject + ".option";
}
std::string subjectSupportKey(const std::string& id, const std::string& subject, std::size_t slot) {
    return "notebook.hypothesis." + id + ".subject." + subject + ".support." + std::to_string(slot);
}

const ClassificationSubject* findSubject(const Hypothesis& h, const std::string& id) {
    for (const auto& subject : h.subjects) {
        if (subject.id == id) {
            return &subject;
        }
    }
    return nullptr;
}

const ArgumentPremise* findPremise(const Hypothesis& h, const std::string& id) {
    for (const auto& premise : h.premises) {
        if (premise.id == id) {
            return &premise;
        }
    }
    return nullptr;
}

} // namespace

NotebookDefinitions parse_notebook_yaml(const std::string& text) {
    YAML::Node root;
    try {
        root = YAML::Load(text);
    } catch (const YAML::Exception& e) {
        fail("notebook.yaml", e.what());
    }
    NotebookDefinitions out;
    out.version = root["version"] ? root["version"].as<int>() : 0;
    if (out.version != 1) {
        fail("notebook.version", "only notebook version 1 is supported");
    }
    const YAML::Node ui = root["ui"];
    out.ui.background = required(ui, "background", "ui");
    out.ui.font = required(ui, "font", "ui");
    out.ui.font_size = optionalUnsigned(ui, "font_size", out.ui.font_size);
    if (out.ui.font_size == 0) {
        fail("notebook.invalid-font-size", "ui.font_size must be positive");
    }
    if (const YAML::Node text_node = ui["text"]) {
        out.ui.text.outline_thickness =
            optionalFloat(text_node, "outline_thickness", out.ui.text.outline_thickness);
        if (out.ui.text.outline_thickness < 0.0f) {
            fail("notebook.invalid-outline-thickness",
                 "ui.text.outline_thickness must not be negative");
        }
        out.ui.text.outline_color =
            color(text_node["outline_color"], out.ui.text.outline_color, "ui.text.outline_color");
    }
    if (const YAML::Node pages = ui["pages"]) {
        out.ui.left_page = rect(pages["left"], out.ui.left_page, "ui.pages.left");
        out.ui.right_page = rect(pages["right"], out.ui.right_page, "ui.pages.right");
    }
    if (const YAML::Node tabs_node = ui["tabs"]) {
        out.ui.tab_font_size = optionalUnsigned(tabs_node, "font_size", out.ui.tab_font_size);
        if (out.ui.tab_font_size == 0) {
            fail("notebook.invalid-font-size", "ui.tabs.font_size must be positive");
        }
        out.ui.tab_idle = color(tabs_node["idle_color"], out.ui.tab_idle, "ui.tabs.idle_color");
        out.ui.tab_hover = color(tabs_node["hover_color"], out.ui.tab_hover, "ui.tabs.hover_color");
        out.ui.tab_selected =
            color(tabs_node["selected_color"], out.ui.tab_selected, "ui.tabs.selected_color");
        out.ui.evidence_tab = tab(tabs_node["evidence"], out.ui.evidence_tab, "ui.tabs.evidence");
        out.ui.hypotheses_tab =
            tab(tabs_node["hypotheses"], out.ui.hypotheses_tab, "ui.tabs.hypotheses");
    }
    out.ui.close = closeControl(ui["close"], out.ui.close);

    std::set<std::string> section_ids;
    for (const YAML::Node& node : root["evidence_sections"]) {
        EvidenceSection section{required(node, "id", "evidence section"),
                                required(node, "label", "evidence section")};
        if (!section_ids.insert(section.id).second) {
            fail("notebook.duplicate-section", "duplicate evidence section '" + section.id + "'");
        }
        out.sections.push_back(std::move(section));
    }

    std::set<std::string> evidence_ids;
    for (const YAML::Node& node : root["evidences"]) {
        Evidence e;
        e.id = required(node, "id", "evidence");
        e.section = required(node, "section", "evidence '" + e.id + "'");
        e.title = required(node, "title", "evidence '" + e.id + "'");
        e.note = required(node, "note", "evidence '" + e.id + "'");
        e.image = node["image"] ? node["image"].as<std::string>() : std::string();
        e.tags = tags(node["tags"]);
        e.initially_discovered =
            node["initially_discovered"] ? node["initially_discovered"].as<bool>() : false;
        if (!evidence_ids.insert(e.id).second) {
            fail("notebook.duplicate-evidence", "duplicate evidence '" + e.id + "'");
        }
        if (section_ids.count(e.section) == 0) {
            fail("notebook.unknown-section", "evidence '" + e.id + "' names unknown section '" + e.section + "'");
        }
        out.evidences.push_back(std::move(e));
    }

    std::set<std::string> hypothesis_ids;
    for (const YAML::Node& node : root["hypotheses"]) {
        Hypothesis h;
        h.id = required(node, "id", "hypothesis");
        h.title = required(node, "title", "hypothesis '" + h.id + "'");
        if (!hypothesis_ids.insert(h.id).second || evidence_ids.count(h.id)) {
            fail("notebook.duplicate-hypothesis", "duplicate hypothesis '" + h.id + "'");
        }
        const std::string type = required(node, "template", "hypothesis '" + h.id + "'");
        h.evidence_tags = tags(node["evidence_tags"]);
        if (type == "classification") {
            h.type = HypothesisTemplate::Classification;
            for (const YAML::Node& subject_node : node["subjects"]) {
                ClassificationSubject subject;
                subject.id = required(subject_node, "id", "classification subject");
                subject.label = required(subject_node, "label", "classification subject '" + subject.id + "'");
                subject.evidence_tags = tags(subject_node["evidence_tags"]);
                subject.support_slots =
                    subject_node["support_slots"] ? subject_node["support_slots"].as<std::size_t>() : 1;
                for (const YAML::Node& option_node : subject_node["options"]) {
                    subject.options.push_back({required(option_node, "id", "classification option"),
                                               required(option_node, "label", "classification option")});
                }
                if (subject.options.empty()) {
                    fail("notebook.missing-options", "classification subject '" + subject.id + "' needs options");
                }
                h.subjects.push_back(std::move(subject));
            }
            if (h.subjects.empty()) {
                fail("notebook.missing-subjects", "classification hypothesis '" + h.id + "' needs subjects");
            }
        } else if (type == "argument") {
            h.type = HypothesisTemplate::Argument;
            for (const YAML::Node& conclusion_node : node["conclusions"]) {
                h.conclusions.push_back({required(conclusion_node, "id", "argument conclusion"),
                                         required(conclusion_node, "label", "argument conclusion")});
            }
            for (const YAML::Node& premise_node : node["premises"]) {
                ArgumentPremise premise;
                premise.id = required(premise_node, "id", "argument premise");
                premise.label = required(premise_node, "label", "argument premise '" + premise.id + "'");
                premise.evidence_tags = tags(premise_node["evidence_tags"]);
                premise.required = premise_node["required"] ? premise_node["required"].as<bool>() : true;
                h.premises.push_back(std::move(premise));
            }
            if (h.conclusions.empty() || h.premises.empty()) {
                fail("notebook.missing-argument", "argument hypothesis '" + h.id + "' needs conclusions and premises");
            }
        } else {
            fail("notebook.template", "unknown hypothesis template '" + type + "'");
        }
        out.hypotheses.push_back(std::move(h));
    }
    return out;
}

NotebookModel::NotebookModel(NotebookDefinitions definitions,
                             pac::core::StateStore& state,
                             pac::core::Diagnostics& log)
    : definitions_(std::move(definitions)), state_(&state), log_(&log) {
    for (const Evidence& e : definitions_.evidences) {
        if (e.initially_discovered && !state_->has(evidenceKey(e.id))) {
            state_->set(evidenceKey(e.id), true);
        }
    }
}

const Evidence* NotebookModel::evidence(const std::string& id) const {
    for (const Evidence& e : definitions_.evidences) {
        if (e.id == id) return &e;
    }
    return nullptr;
}
const Hypothesis* NotebookModel::hypothesis(const std::string& id) const {
    for (const Hypothesis& h : definitions_.hypotheses) {
        if (h.id == id) return &h;
    }
    return nullptr;
}
bool NotebookModel::discoverEvidence(const std::string& id) {
    if (!evidence(id)) {
        log_->warn("notebook: unknown evidence '" + id + "'");
        return false;
    }
    state_->set(evidenceKey(id), true);
    return true;
}
bool NotebookModel::hasEvidence(const std::string& id) const {
    const Evidence* e = evidence(id);
    if (!e) return false;
    const auto value = state_->get(evidenceKey(id));
    if (!value) return e->initially_discovered;
    return std::holds_alternative<bool>(*value) && std::get<bool>(*value);
}
std::size_t NotebookModel::discoveredEvidenceCount() const {
    return std::count_if(definitions_.evidences.begin(), definitions_.evidences.end(),
                         [this](const Evidence& e) { return hasEvidence(e.id); });
}
std::size_t NotebookModel::discoveredEvidenceCountByTag(const std::string& tag) const {
    return std::count_if(definitions_.evidences.begin(), definitions_.evidences.end(),
                         [this, &tag](const Evidence& e) { return e.tags.count(tag) && hasEvidence(e.id); });
}
std::string NotebookModel::stateString(const std::string& key) const {
    const auto value = state_->get(key);
    return value && std::holds_alternative<std::string>(*value) ? std::get<std::string>(*value) : std::string();
}
void NotebookModel::setOrErase(const std::string& key, const std::string& value) {
    if (value.empty()) state_->erase(key); else state_->set(key, value);
}
std::string NotebookModel::hypothesisConclusion(const std::string& id) const {
    const Hypothesis* h = hypothesis(id);
    if (!h || h->type != HypothesisTemplate::Argument) return {};
    const std::string selected = stateString(conclusionKey(id));
    return std::any_of(h->conclusions.begin(), h->conclusions.end(),
                       [&selected](const ArgumentConclusion& c) { return c.id == selected; }) ? selected : std::string();
}
std::string NotebookModel::hypothesisEvidence(const std::string& id, const std::string& premise) const {
    const Hypothesis* h = hypothesis(id);
    const ArgumentPremise* p = h ? findPremise(*h, premise) : nullptr;
    if (!p) return {};
    const std::string selected = stateString(premiseKey(id, premise));
    const Evidence* e = evidence(selected);
    return e && hasEvidence(selected) && intersects(e->tags, p->evidence_tags) ? selected : std::string();
}
bool NotebookModel::hasHypothesisEvidence(const std::string& id, const std::string& premise,
                                          const std::string& evidence_id) const {
    return hypothesisEvidence(id, premise) == evidence_id;
}
bool NotebookModel::hasArgumentCandidates(const std::string& id, const std::string& premise) const {
    const Hypothesis* h = hypothesis(id);
    const ArgumentPremise* p = h ? findPremise(*h, premise) : nullptr;
    return p && !argumentCandidates(*h, *p).empty();
}
bool NotebookModel::hasClassification(const std::string& id, const std::string& subject,
                                      const std::string& option) const {
    const Hypothesis* h = hypothesis(id);
    const ClassificationSubject* s = h ? findSubject(*h, subject) : nullptr;
    if (!s) return false;
    return stateString(subjectOptionKey(id, subject)) == option &&
           std::any_of(s->options.begin(), s->options.end(), [&option](const auto& o) { return o.id == option; });
}
bool NotebookModel::hasClassificationSupport(const std::string& id, const std::string& subject,
                                             const std::string& evidence_id) const {
    const Hypothesis* h = hypothesis(id);
    const ClassificationSubject* s = h ? findSubject(*h, subject) : nullptr;
    if (!s) return false;
    const Evidence* e = evidence(evidence_id);
    if (!e || !hasEvidence(evidence_id) || !intersects(e->tags, h->evidence_tags) ||
        !intersects(e->tags, s->evidence_tags)) {
        return false;
    }
    for (std::size_t slot = 0; slot < s->support_slots; ++slot) {
        if (stateString(subjectSupportKey(id, subject, slot)) == evidence_id) return true;
    }
    return false;
}
std::string NotebookModel::classificationSupport(const std::string& id,
                                                 const std::string& subject,
                                                 std::size_t slot) const {
    const Hypothesis* h = hypothesis(id);
    const ClassificationSubject* s = h ? findSubject(*h, subject) : nullptr;
    if (!s || slot >= s->support_slots) return {};
    const std::string selected = stateString(subjectSupportKey(id, subject, slot));
    return hasClassificationSupport(id, subject, selected) ? selected : std::string();
}
bool NotebookModel::hasClassificationCandidates(const std::string& id,
                                                const std::string& subject) const {
    const Hypothesis* h = hypothesis(id);
    const ClassificationSubject* s = h ? findSubject(*h, subject) : nullptr;
    return s && !classificationCandidates(*h, *s).empty();
}
std::set<std::string> NotebookModel::selectedSupports(const Hypothesis& h, const std::string& except_key) const {
    std::set<std::string> out;
    if (h.type == HypothesisTemplate::Argument) {
        for (const auto& p : h.premises) {
            const std::string key = premiseKey(h.id, p.id);
            if (key != except_key) out.insert(stateString(key));
        }
    } else {
        for (const auto& subject : h.subjects) for (std::size_t slot = 0; slot < subject.support_slots; ++slot) {
            const std::string key = subjectSupportKey(h.id, subject.id, slot);
            if (key != except_key) out.insert(stateString(key));
        }
    }
    out.erase("");
    return out;
}
std::vector<std::string> NotebookModel::argumentCandidates(const Hypothesis&, const ArgumentPremise& p) const {
    std::vector<std::string> out;
    for (const auto& e : definitions_.evidences) if (hasEvidence(e.id) && intersects(e.tags, p.evidence_tags)) out.push_back(e.id);
    return out;
}
std::vector<std::string> NotebookModel::classificationCandidates(const Hypothesis& h, const ClassificationSubject& s) const {
    std::vector<std::string> out;
    for (const auto& e : definitions_.evidences)
        if (hasEvidence(e.id) && intersects(e.tags, h.evidence_tags) && intersects(e.tags, s.evidence_tags)) out.push_back(e.id);
    return out;
}
std::string NotebookModel::cycle(const std::string& key, std::vector<std::string> candidates, const Hypothesis& h) {
    const auto used = selectedSupports(h, key);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [&used](const std::string& id) { return used.count(id); }), candidates.end());
    const std::string current = stateString(key);
    const auto it = std::find(candidates.begin(), candidates.end(), current);
    const std::string next = it == candidates.end() ? (candidates.empty() ? "" : candidates.front())
                                                    : (std::next(it) == candidates.end() ? "" : *std::next(it));
    setOrErase(key, next);
    return next;
}
std::string NotebookModel::cycleArgumentEvidence(const std::string& id, const std::string& premise) {
    const Hypothesis* h = hypothesis(id);
    const ArgumentPremise* p = h ? findPremise(*h, premise) : nullptr;
    return p ? cycle(premiseKey(id, premise), argumentCandidates(*h, *p), *h) : std::string();
}
std::string NotebookModel::cycleClassificationSupport(const std::string& id, const std::string& subject, std::size_t slot) {
    const Hypothesis* h = hypothesis(id);
    const ClassificationSubject* s = h ? findSubject(*h, subject) : nullptr;
    return s && slot < s->support_slots ? cycle(subjectSupportKey(id, subject, slot), classificationCandidates(*h, *s), *h) : std::string();
}
void NotebookModel::setConclusion(const std::string& id, const std::string& conclusion) {
    const Hypothesis* h = hypothesis(id);
    if (!h || !std::any_of(h->conclusions.begin(), h->conclusions.end(), [&conclusion](const auto& c) { return c.id == conclusion; })) return;
    setOrErase(conclusionKey(id), conclusion);
}
void NotebookModel::setClassification(const std::string& id, const std::string& subject, const std::string& option) {
    const Hypothesis* h = hypothesis(id);
    const ClassificationSubject* s = h ? findSubject(*h, subject) : nullptr;
    if (!s || !std::any_of(s->options.begin(), s->options.end(), [&option](const auto& o) { return o.id == option; })) return;
    setOrErase(subjectOptionKey(id, subject), option);
}
bool NotebookModel::isHypothesisComplete(const std::string& id) const {
    const Hypothesis* h = hypothesis(id);
    if (!h) return false;
    if (h->type == HypothesisTemplate::Argument) {
        if (hypothesisConclusion(id).empty()) return false;
        std::set<std::string> selected;
        for (const auto& p : h->premises) {
            const std::string evidence_id = hypothesisEvidence(id, p.id);
            if (p.required && evidence_id.empty()) return false;
            if (!evidence_id.empty() && !selected.insert(evidence_id).second) return false;
        }
        return true;
    }
    std::set<std::string> selected;
    for (const auto& s : h->subjects) {
        const std::string selected_option = stateString(subjectOptionKey(id, s.id));
        if (!std::any_of(s.options.begin(), s.options.end(), [&selected_option](const auto& o) { return o.id == selected_option; })) return false;
        for (std::size_t slot = 0; slot < s.support_slots; ++slot) {
            const std::string evidence_id = stateString(subjectSupportKey(id, s.id, slot));
            if (!hasClassificationSupport(id, s.id, evidence_id) || !selected.insert(evidence_id).second) return false;
        }
    }
    return true;
}
bool NotebookModel::hypothesisHasTagSupport(const std::string& id, const std::string& tag) const {
    const Hypothesis* h = hypothesis(id);
    if (!h) return false;
    for (const std::string& selected : selectedSupports(*h, "")) {
        const Evidence* e = evidence(selected);
        if (e && hasEvidence(selected) && e->tags.count(tag)) return true;
    }
    return false;
}
std::size_t NotebookModel::countHypothesisSupports(const std::string& id) const {
    const Hypothesis* h = hypothesis(id);
    if (!h) return 0;
    std::size_t count = 0;
    for (const std::string& selected : selectedSupports(*h, "")) if (evidence(selected) && hasEvidence(selected)) ++count;
    return count;
}
void NotebookModel::reset() {
    state_->erase_prefix("notebook.");
    for (const Evidence& e : definitions_.evidences) if (e.initially_discovered) state_->set(evidenceKey(e.id), true);
}

} // namespace ingreso::notebook
