#pragma once

#include "engine/pnc/command.hpp"

#include <functional>
#include <string>

namespace pac::pnc {

/// UI meaning after widget-specific hit testing. RoomScene coordinates services
/// from these values and never needs to inspect ScummPanel geometry or intents.
struct RoomUiIntent {
    enum class Kind {
        NONE,
        SELECT_VERB,
        SELECT_INVENTORY_ITEM,
        CHANGE_INVENTORY_PAGE,
        HOVER_VERB,
        HOVER_INVENTORY_ITEM,
        CLEAR_COMMAND_HOVER,
        CANCEL_COMMAND,
        DISMISS_SPEECH,
        OPEN_SETTINGS,
        OPEN_MENU,
        OPEN_NOTEBOOK,
        PUSH_SCENE,
        CHOOSE_DIALOG_OPTION,
        CHANGE_DIALOG_PAGE,
    };

    Kind kind = Kind::NONE;
    Verb verb = Verb::LOOK_AT;
    std::string id;
    int index = 0;
    std::string scene;
    std::string state_key;
};

using RoomUiIntentSink = std::function<void(const RoomUiIntent&)>;

} // namespace pac::pnc
