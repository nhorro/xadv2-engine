#include "notebook/notebook_module.hpp"

#include "notebook/notebook_model.hpp"
#include "notebook/notebook_scene.hpp"

#include "engine/core/engine_context.hpp"
#include "engine/core/manifest.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scene_factory.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scripting.hpp"

#include <sol/sol.hpp>

#include <stdexcept>
#include <utility>

namespace ingreso::notebook {

void bindNotebookLua(pac::core::Scripting& scripting,
                     NotebookModel& model,
                     pac::core::SceneManager& scenes,
                     const std::string& scene_id) {
    sol::state& lua = scripting.lua();
    auto discover = [&model](const std::string& id) { return model.discoverEvidence(id); };
    auto has_evidence = [&model](const std::string& id) { return model.hasEvidence(id); };
    auto count = [&model]() { return model.discoveredEvidenceCount(); };
    auto count_tag = [&model](const std::string& tag) { return model.discoveredEvidenceCountByTag(tag); };
    auto conclusion = [&model](const std::string& id) { return model.hypothesisConclusion(id); };
    auto hypothesis_evidence = [&model](const std::string& id, const std::string& premise) {
        return model.hypothesisEvidence(id, premise);
    };
    auto has_hypothesis_evidence = [&model](const std::string& id, const std::string& premise, const std::string& evidence) {
        return model.hasHypothesisEvidence(id, premise, evidence);
    };
    auto complete = [&model](const std::string& id) { return model.isHypothesisComplete(id); };
    auto classification = [&model](const std::string& id, const std::string& subject, const std::string& option) {
        return model.hasClassification(id, subject, option);
    };
    auto classification_support = [&model](const std::string& id, const std::string& subject, const std::string& evidence) {
        return model.hasClassificationSupport(id, subject, evidence);
    };
    auto tag_support = [&model](const std::string& id, const std::string& tag) {
        return model.hypothesisHasTagSupport(id, tag);
    };
    auto supports = [&model](const std::string& id) { return model.countHypothesisSupports(id); };
    auto open = [&scenes, scene_id]() { scenes.push_scene(scene_id); };

    lua.set_function("discover_evidence", discover);
    lua.set_function("has_evidence", has_evidence);
    lua.set_function("get_discovered_evidence_count", count);
    lua.set_function("get_discovered_evidence_count_by_tag", count_tag);
    lua.set_function("get_hypothesis_conclusion", conclusion);
    lua.set_function("get_hypothesis_evidence", hypothesis_evidence);
    lua.set_function("has_hypothesis_evidence", has_hypothesis_evidence);
    lua.set_function("is_hypothesis_complete", complete);
    lua.set_function("has_classification", classification);
    lua.set_function("has_classification_support", classification_support);
    lua.set_function("hypothesis_has_tag_support", tag_support);
    lua.set_function("count_hypothesis_supports", supports);
    lua.set_function("open_notebook", open);

    sol::table notebook = lua.create_named_table("Notebook");
    notebook.set_function("discoverEvidence", discover);
    notebook.set_function("hasEvidence", has_evidence);
    notebook.set_function("getDiscoveredEvidenceCount", count);
    notebook.set_function("getDiscoveredEvidenceCountByTag", count_tag);
    notebook.set_function("getHypothesisConclusion", conclusion);
    notebook.set_function("getHypothesisEvidence", hypothesis_evidence);
    notebook.set_function("hasHypothesisEvidence", has_hypothesis_evidence);
    notebook.set_function("isHypothesisComplete", complete);
    notebook.set_function("hasClassification", classification);
    notebook.set_function("hasClassificationSupport", classification_support);
    notebook.set_function("hypothesisHasTagSupport", tag_support);
    notebook.set_function("countHypothesisSupports", supports);
    notebook.set_function("open", open);
}

NotebookModule::NotebookModule(std::string scene_id) : scene_id_(std::move(scene_id)) {}

NotebookModule::~NotebookModule() = default;

void NotebookModule::registerScenes(pac::core::SceneFactory& factory) {
    factory.register_type("IngresoNotebook", [this](pac::core::EngineContext& ctx,
                                                    const pac::core::SceneParams& params) {
        return std::make_unique<NotebookScene>(ctx, params, model());
    });
}

void NotebookModule::configure(pac::core::EngineContext& ctx, const pac::core::Manifest& manifest) {
    const pac::core::SceneDesc* scene = manifest.find_scene(scene_id_);
    if (!scene) {
        throw std::runtime_error("notebook: manifest has no scene '" + scene_id_ + "'");
    }
    const std::string definitions_path = scene->parameters.get_or("data", "");
    if (definitions_path.empty()) {
        throw std::runtime_error("notebook: scene '" + scene_id_ + "' needs parameters.data");
    }
    model_ = std::make_unique<NotebookModel>(
        parse_notebook_yaml(ctx.resources.read_text(definitions_path)), ctx.state, ctx.log);
    bindNotebookLua(ctx.scripting, *model_, ctx.scenes, scene_id_);
}

NotebookModel& NotebookModule::model() {
    if (!model_) throw std::runtime_error("notebook module was not configured");
    return *model_;
}

} // namespace ingreso::notebook
