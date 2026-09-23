#pragma once

#include "engine/core/control.hpp"

namespace pac::pnc {

class RoomScene;

class RoomControlService final : public pac::core::ControlService {
public:
    explicit RoomControlService(RoomScene& scene);
    [[nodiscard]] std::vector<pac::core::ControlMethod> methods() const override;
    pac::core::ControlResult invoke(std::string_view method,
                                    const pac::core::ControlValue& params) override;

private:
    RoomScene& scene_;
};

} // namespace pac::pnc
