#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace pac::core {

class Scene;
class SceneParams;
struct EngineContext;

/// Constructs scenes from a manifest `type` string. Built-in genre types are
/// registered by the point-and-click layer; games may register custom types.
class SceneFactory {
public:
    using Creator = std::function<std::unique_ptr<Scene>(EngineContext&, const SceneParams&)>;

    void register_type(std::string type, Creator creator);
    bool has(const std::string& type) const;

    /// Returns nullptr for an unknown type (the caller logs with scene context).
    std::unique_ptr<Scene>
    create(const std::string& type, EngineContext& ctx, const SceneParams& params) const;

private:
    std::map<std::string, Creator> creators_;
};

} // namespace pac::core
