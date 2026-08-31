#pragma once

#include "engine/pnc/command_state.hpp"
#include "engine/pnc/inventory.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pac::pnc {

enum class RoomInteractionMode { COMMAND, DIALOG, BLOCKED, MENU };

/// Presentation snapshot published by a live room. Values and IDs only: widgets
/// never retain pointers into RoomRuntime across a room change.
struct RoomUiState {
    RoomInteractionMode mode = RoomInteractionMode::COMMAND;
    bool speech_active = false;
    CommandState command;
    InventoryModel inventory;
    std::vector<std::string> dialog_options;
    int dialog_page = 0;
    std::map<std::string, bool> widget_visibility;

    [[nodiscard]] bool widget_visible(const std::string& id) const {
        const auto it = widget_visibility.find(id);
        return it == widget_visibility.end() || it->second;
    }
};

class RoomUiStateStream {
public:
    using Listener = std::function<void(const RoomUiState&)>;

    class Subscription {
    public:
        Subscription() = default;
        ~Subscription();
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;
        void reset();

    private:
        friend class RoomUiStateStream;
        Subscription(RoomUiStateStream* stream, std::size_t id) : stream_(stream), id_(id) {}
        RoomUiStateStream* stream_ = nullptr;
        std::size_t id_ = 0;
    };

    [[nodiscard]] Subscription subscribe(Listener listener);
    void publish(const RoomUiState& state) const;

private:
    void unsubscribe(std::size_t id);

    std::size_t next_id_ = 1;
    std::map<std::size_t, Listener> listeners_;
};

} // namespace pac::pnc
