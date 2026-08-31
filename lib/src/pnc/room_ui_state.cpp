#include "engine/pnc/room_ui_state.hpp"

#include <utility>

namespace pac::pnc {

RoomUiStateStream::Subscription::~Subscription() {
    reset();
}

RoomUiStateStream::Subscription::Subscription(Subscription&& other) noexcept
    : stream_(other.stream_), id_(other.id_) {
    other.stream_ = nullptr;
    other.id_ = 0;
}

RoomUiStateStream::Subscription&
RoomUiStateStream::Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        reset();
        stream_ = other.stream_;
        id_ = other.id_;
        other.stream_ = nullptr;
        other.id_ = 0;
    }
    return *this;
}

void RoomUiStateStream::Subscription::reset() {
    if (stream_) {
        stream_->unsubscribe(id_);
        stream_ = nullptr;
        id_ = 0;
    }
}

RoomUiStateStream::Subscription RoomUiStateStream::subscribe(Listener listener) {
    const std::size_t id = next_id_++;
    listeners_.emplace(id, std::move(listener));
    return Subscription(this, id);
}

void RoomUiStateStream::publish(const RoomUiState& state) const {
    std::vector<Listener> snapshot;
    snapshot.reserve(listeners_.size());
    for (const auto& [id, listener] : listeners_) {
        (void) id;
        snapshot.push_back(listener);
    }
    for (const Listener& listener : snapshot) {
        listener(state);
    }
}

void RoomUiStateStream::unsubscribe(std::size_t id) {
    listeners_.erase(id);
}

} // namespace pac::pnc
