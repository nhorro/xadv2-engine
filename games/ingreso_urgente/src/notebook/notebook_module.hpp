#pragma once

#include <memory>
#include <string>

namespace pac::core {
struct EngineContext;
struct Manifest;
class SceneFactory;
class SceneManager;
class Scripting;
}

namespace ingreso::notebook {

class NotebookModel;

void bindNotebookLua(pac::core::Scripting& scripting,
                     NotebookModel& model,
                     pac::core::SceneManager& scenes,
                     const std::string& scene_id);

class NotebookModule {
public:
    explicit NotebookModule(std::string scene_id);
    ~NotebookModule();
    void registerScenes(pac::core::SceneFactory& factory);
    void configure(pac::core::EngineContext& ctx, const pac::core::Manifest& manifest);
    NotebookModel& model();

private:
    std::string scene_id_;
    std::unique_ptr<NotebookModel> model_;
};

} // namespace ingreso::notebook
