#include "engine/pnc/room_input_router.hpp"

namespace pac::pnc {

void RoomInputRouter::add(RoomInputLayer& layer) {
    layers_.push_back(&layer);
}

void RoomInputRouter::clear() {
    layers_.clear();
}

InputResult RoomInputRouter::route(const RoutedInput& input) {
    for (RoomInputLayer* layer : layers_) {
        if (layer->handle(input) == InputResult::CONSUMED) {
            return InputResult::CONSUMED;
        }
    }
    return InputResult::PASS;
}

} // namespace pac::pnc
