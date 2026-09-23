#pragma once

#include "engine/core/control.hpp"

namespace pac::core {

class SceneManager;
class Scripting;

class EngineControlService final : public ControlService {
public:
    EngineControlService(ControlRouter& router, SceneManager& scenes);
    [[nodiscard]] std::vector<ControlMethod> methods() const override;
    ControlResult invoke(std::string_view method, const ControlValue& params) override;

private:
    ControlRouter& router_;
    SceneManager& scenes_;
};

class LuaControlService final : public ControlService {
public:
    explicit LuaControlService(Scripting& scripting);
    [[nodiscard]] std::vector<ControlMethod> methods() const override;
    ControlResult invoke(std::string_view method, const ControlValue& params) override;

private:
    Scripting& scripting_;
};

} // namespace pac::core
