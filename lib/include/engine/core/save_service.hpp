#pragma once

#include "engine/core/game_state.hpp"

#include <SFML/Graphics/Image.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace pac::core {

/// A light header read off a save slot for the picker UI (issue #108): the
/// player's description (if any) and the wall-clock seconds at save time. The
/// UI falls back to `last_write_time` when `saved_at` is 0 (an older save
/// written before issue #108).
struct SlotSummary {
    std::string description;
    std::int64_t saved_at = 0;
};

class Diagnostics;

/// MVP save system per design 01 §R8 + 02 §"Make persistent state explicit":
/// three manual slots (1-3) plus one autosave slot (0), YAML on disk via
/// yaml-cpp. The directory is supplied at construction (typically
/// `user_data_dir("xadv2-engine") / "saves"`); it is created lazily on first
/// write.
///
/// MVP scope: no thumbnails, no script-facing `save_game()`, no slot metadata
/// other than file mtime (used by `latest_slot()` for Continue). The format
/// carries a `save_version` int so future bumps can refuse or migrate older
/// saves.
class SaveService {
public:
    /// Slot 0 is the engine-managed autosave; 1, 2, 3 are manual slots.
    static constexpr int kAutosaveSlot = 0;
    static constexpr int kSlotCount = 4;

    SaveService(std::filesystem::path dir, Diagnostics& log);

    /// Resolved path for `slot`'s file (regardless of whether it exists).
    [[nodiscard]] std::filesystem::path slot_path(int slot) const;

    /// Resolved path for `slot`'s thumbnail sidecar (#119) — `slot_N.thumb.png`
    /// alongside the YAML save. Regardless of whether the file exists.
    [[nodiscard]] std::filesystem::path thumbnail_path(int slot) const;

    /// True if the slot file exists and is a regular file.
    [[nodiscard]] bool slot_exists(int slot) const;

    /// True if the slot has a thumbnail sidecar on disk (#119).
    [[nodiscard]] bool slot_has_thumbnail(int slot) const;

    /// Write `state` to `slot`, and optionally `thumbnail` as a sidecar PNG
    /// (#119). Returns false (and logs) on slot-out-of-range or I/O failure
    /// for the main save (a thumbnail write failure is logged but does not
    /// fail the save). Creates the save directory if needed.
    bool save(int slot, const GameState& state, const sf::Image* thumbnail = nullptr);

    /// Read `slot`. Returns nullopt on a missing file, malformed YAML, or
    /// unsupported `save_version`.
    [[nodiscard]] std::optional<GameState> load(int slot);

    /// Slot with the most recent mtime, or nullopt if no slot file exists.
    /// Continue uses this to pick the latest save across the autosave and
    /// the manual slots.
    [[nodiscard]] std::optional<int> latest_slot() const;

    /// Cheap header-only read of `slot` for the picker UI: parses the YAML
    /// but only keeps `description` + `saved_at`. Returns nullopt on a
    /// missing or malformed file (an inhabited slot whose header parsed but
    /// has neither field still returns a zero-initialized summary, so the UI
    /// can detect "exists but with no metadata" via `slot_exists()`).
    [[nodiscard]] std::optional<SlotSummary> slot_summary(int slot) const;

    /// Stage `state` as the GameState the next scene should restore from.
    /// Used by TitleScreen's "Continue" button: it loads a slot and stages
    /// the result here, then triggers the scene change; the new RoomScene
    /// consumes the staged state via `take_pending_restore()`.
    void stage_restore(GameState state);

    /// Take and clear the staged restore, if any. Returns nullopt when
    /// there's nothing pending. RoomScene::enter() calls this to decide
    /// whether to start fresh (manifest's `start_room`) or restore.
    [[nodiscard]] std::optional<GameState> take_pending_restore();

    [[nodiscard]] bool has_pending_restore() const { return pending_restore_.has_value(); }

    /// Stage a snapshot for the save UI to write (issue #108). RoomScene snaps
    /// the current state and stages it before pushing the save scene; the save
    /// scene later edits the description on it and calls `save(slot, *snap)`.
    /// One pending snap at a time — pushing a new one overwrites.
    void stage_pending_snap(GameState state);

    /// Take and clear the pending snap, if any. The save scene calls this when
    /// the player confirms a slot; if the snap has gone stale (somehow popped
    /// without saving), the caller sees `nullopt` and bails.
    [[nodiscard]] std::optional<GameState> take_pending_snap();

    /// True when a snap is staged (the save UI uses this to enable its buttons).
    [[nodiscard]] bool has_pending_snap() const { return pending_snap_.has_value(); }

    /// Stage a thumbnail image (#119) alongside the pending snap. RoomScene
    /// calls this with `EngineContext::thumbnail.image()` so the save scene
    /// can write the sidecar PNG. An empty image clears any prior staging.
    void stage_pending_thumbnail(sf::Image image);

    /// Take and clear the staged thumbnail. Returns an empty image when none
    /// was staged (the save still writes; the slot just gets no sidecar PNG).
    [[nodiscard]] sf::Image take_pending_thumbnail();

private:
    [[nodiscard]] static bool slot_in_range(int slot);

    std::filesystem::path dir_;
    Diagnostics* log_;
    std::optional<GameState> pending_restore_;
    std::optional<GameState> pending_snap_;
    sf::Image pending_thumbnail_;
};

} // namespace pac::core
