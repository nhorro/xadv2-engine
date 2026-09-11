#pragma once

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

#include <string>

namespace pac::pnc {

struct DirectRoomUiInteractionConfig {
    float drag_threshold = 12.0f;
    float long_press_seconds = 0.48f;
};

struct DirectRoomUiContextMenuConfig {
    sf::Vector2f cell_size{76.0f, 62.0f};
    float gap = 6.0f;
    float padding = 8.0f;
};

struct DirectRoomUiInventoryConfig {
    sf::FloatRect panel{522.0f, 616.0f, 590.0f, 88.0f};
    sf::FloatRect grid{586.0f, 624.0f, 462.0f, 72.0f};
    sf::FloatRect previous{534.0f, 636.0f, 40.0f, 48.0f};
    sf::FloatRect next{1060.0f, 636.0f, 40.0f, 48.0f};
    int rows = 1;
    int columns = 6;
    sf::Vector2f cell_gap{6.0f, 0.0f};
    bool compact_single_page = false;
    float compact_padding = 10.0f;
};

/// Optional transparent icon atlas. Cell assignments are explicit so games can
/// replace the visual language without changing engine code. A missing sheet or
/// a negative cell falls back to the built-in vector-like primitives.
struct DirectRoomUiIconsConfig {
    std::string sheet;
    int columns = 4;
    int rows = 4;
    int bag = 0;
    int menu = 1;
    int previous = 2;
    int next = 3;
    int look_at = 4;
    int talk_to = 5;
    int pick_up = 6;
    int open = 7;
    int close = 8;
    int push = 9;
    int pull = 10;
    int use = 11;
    int give = 12;
};

struct DirectRoomUiStyle {
    sf::Color panel_background{21, 22, 23, 230};
    sf::Color panel_border{201, 152, 46, 230};
    sf::Color slot_background{22, 33, 43, 210};
    sf::Color slot_border{91, 96, 99, 220};
    sf::Color hover_border{43, 183, 214, 255};
    sf::Color button_background{21, 22, 23, 218};
    sf::Color button_border{225, 209, 171, 235};
    sf::Color button_hover{22, 33, 43, 245};
    sf::Color text{225, 209, 171, 255};
    sf::Color disabled_text{143, 135, 112, 180};
    sf::Color action_background{21, 22, 23, 205};
    sf::Color action_outline{0, 0, 0, 235};
    sf::Color notification{245, 193, 72, 255};
    float border_thickness = 2.0f;
    unsigned text_size = 22;
    unsigned action_text_size = 23;
    float action_outline_thickness = 1.5f;
};

/// Screen-space layout for the optional direct room composer. All rectangles are
/// expressed in `design_size` coordinates and scaled to the virtual resolution.
struct DirectRoomUiConfig {
    sf::Vector2f design_size{1280.0f, 720.0f};
    DirectRoomUiInteractionConfig interaction;
    sf::FloatRect bag_button{1136.0f, 632.0f, 64.0f, 64.0f};
    sf::FloatRect menu_button{1208.0f, 632.0f, 48.0f, 64.0f};
    sf::FloatRect action_text{300.0f, 566.0f, 680.0f, 42.0f};
    sf::Vector2f action_text_offset{18.0f, 24.0f};
    bool action_follows_cursor = false;
    DirectRoomUiInventoryConfig inventory;
    DirectRoomUiContextMenuConfig context_menu;
    DirectRoomUiIconsConfig icons;
    DirectRoomUiStyle style;
};

/// Parse a `direct_room_ui:` YAML document. Throws DataError on malformed data.
[[nodiscard]] DirectRoomUiConfig parse_direct_room_ui_config(const std::string& yaml_text);

} // namespace pac::pnc
