#pragma once

#include "engine/core/scene.hpp"
#include "engine/core/scripting.hpp"
#include "engine/geom/geometry.hpp"
#include "engine/pnc/case_resolution.hpp"
#include "engine/pnc/case_resolution_runtime.hpp"

#include <string>

namespace sf {
class Font;
}
namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// Golden-Idol-style deduction screen: terms are selected from a paginated bank
/// and placed into tag-constrained polygon slots over an authored background.
class CaseResolutionScene final : public pac::core::Scene {
public:
    CaseResolutionScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);
    void enter() override;
    void leave() override;
    void handle_event(const sf::Event& event) override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

private:
    void exit();
    void activate(geom::Point point);
    [[nodiscard]] std::size_t page_count() const;

    pac::core::EngineContext& ctx_;
    std::string data_path_, terms_path_, logic_path_, on_exit_, on_solve_;
    const sf::Font* font_ = nullptr;
    CaseResolutionData data_;
    CaseTermBank bank_;
    CaseAssignments assignments_;
    bool loaded_ = false;
    std::size_t page_ = 0;
    std::string selected_term_;
    geom::Point mouse_{-1.0f, -1.0f};
    float feedback_left_ = 0.0f;
    bool feedback_success_ = false;
    std::string exit_status_ = "cancelled";
    bool exit_hook_run_ = false;
    CaseResolutionRuntime runtime_;
    pac::core::ScopeId case_scope_ = 0;
};

} // namespace pac::pnc
