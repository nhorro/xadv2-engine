#include "engine/core/scene_factory.hpp"

#include "engine/core/scene.hpp"

#include <utility>

namespace pac::core {

void SceneFactory::register_type(std::string type, Creator creator) {
    creators_[std::move(type)] = std::move(creator);
}

bool SceneFactory::has(const std::string& type) const {
    return creators_.find(type) != creators_.end();
}

std::unique_ptr<Scene>
SceneFactory::create(const std::string& type, EngineContext& ctx, const SceneParams& params) const {
    const auto it = creators_.find(type);
    if (it == creators_.end()) {
        return nullptr;
    }
    return it->second(ctx, params);
}

} // namespace pac::core
