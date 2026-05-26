#pragma once

#include "engine/core/save_service.hpp"
#include "engine/core/scene.hpp"

#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/String.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {
class Font;
}

namespace pac::core {
struct EngineContext;
class SceneParams;
} // namespace pac::core

namespace pac::pnc {

/// Save / Load picker (issue #108). One scene type, two manifest entries that
/// differ only by `parameters.mode`: `save` or `load`. Shares the layout with
/// SettingsScene (full-screen image background, the same image is fine).
///
/// The screen lists the autosave slot (read-only) plus the manual slots. Each
/// row carries a thumbnail placeholder, the slot label, the existing
/// description, and a date/time fallback. In **save** mode every manual row has
/// its own text field for the description (typing is per-row, focus follows
/// click); clicking that row's "Save" button writes the staged snapshot. In
/// **load** mode every row gets a "Load" button (disabled when the slot is
/// empty).
///
/// The save snapshot is staged on `SaveService` by the caller (typically the
/// in-game pause menu) before the scene is pushed; the scene takes it on the
/// first Save click. The load flow restages the loaded GameState via
/// `SaveService::stage_restore` and triggers a scene change to the room scene —
/// the same hand-off TitleScreen's Continue uses.
///
/// Configured from the manifest scene `parameters`:
///   mode        (req)        `save` | `load`
///   background  (opt path)   full-screen image; dark fill when empty
///   font        (opt path)   UI font
///   font_size   (opt int)    base UI font size
///   room_scene  (opt id)     scene id to `goto_scene` on load confirm (default `room_view`)
class SaveLoadScene : public pac::core::Scene {
public:
    SaveLoadScene(pac::core::EngineContext& ctx, const pac::core::SceneParams& params);

    void handle_event(const sf::Event& event) override;
    void update(float dt) override;
    void draw(sf::RenderTarget& target) const override;

private:
    enum class Mode { SAVE, LOAD };

    struct SlotView {
        int slot = 0;
        bool exists = false;
        std::string description; // from disk (empty when not saved or autosave)
        std::int64_t saved_at = 0;
        sf::FloatRect row;        // hit rect of the whole row (for click-to-focus)
        sf::FloatRect input_rect; // text-input rect (save mode, manual slots)
        sf::FloatRect button;     // save/load button rect
        // User-edited description (save mode), stored as 32-bit code points so
        // backspace drops one glyph, not a single UTF-8 byte. Seeded from the
        // disk `description` so a re-save keeps the previous text by default.
        sf::String draft;
        /// Loaded thumbnail sidecar (#119). `nullptr` when the slot has no
        /// thumbnail on disk (older save, autosave whose capture never landed)
        /// — the picker then draws the placeholder. Heap-allocated so its
        /// address is stable while the SlotView is moved around the vector.
        std::unique_ptr<sf::Texture> thumbnail;
    };

    void refresh_summaries(); // read SlotSummary for each slot

    // Layout helpers shared by draw + hit-testing. Computes the column rects
    // for a row's top y. Layout: [thumb][label/desc][text input?][button]
    void compute_row_rects(float top, SlotView& v) const;

    // Click routing.
    void on_click(float vx, float vy);
    void on_text(sf::Uint32 codepoint);
    void on_key(sf::Keyboard::Key key);

    // Actions.
    void save_into(SlotView& view);
    void load_from(const SlotView& view);
    void cancel(); // pop back

    // Format a Unix timestamp as "YYYY-MM-DD HH:MM"; falls back to file mtime
    // when `saved_at` is 0 (an older save).
    std::string format_when(const SlotView& view) const;

    pac::core::EngineContext& ctx_;
    Mode mode_ = Mode::SAVE;
    const sf::Font* font_ = nullptr; // owned by ResourceCache; null if unavailable
    std::string background_path_;
    std::string room_scene_id_;
    unsigned font_size_ = 22;

    std::vector<SlotView> rows_;
    int focused_row_ = -1; // index into rows_, or -1
    int hovered_row_ = -1; // for cursor affordance
    sf::FloatRect back_button_{};
    bool back_hovered_ = false;
};

} // namespace pac::pnc
