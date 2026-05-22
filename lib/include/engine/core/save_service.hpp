#pragma once

#include "engine/core/game_state.hpp"

#include <filesystem>
#include <optional>

namespace pac::core {

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

    /// True if the slot file exists and is a regular file.
    [[nodiscard]] bool slot_exists(int slot) const;

    /// Write `state` to `slot`. Returns false (and logs) on slot-out-of-range
    /// or I/O failure. Creates the save directory if needed.
    bool save(int slot, const GameState& state);

    /// Read `slot`. Returns nullopt on a missing file, malformed YAML, or
    /// unsupported `save_version`.
    [[nodiscard]] std::optional<GameState> load(int slot);

    /// Slot with the most recent mtime, or nullopt if no slot file exists.
    /// Continue uses this to pick the latest save across the autosave and
    /// the manual slots.
    [[nodiscard]] std::optional<int> latest_slot() const;

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

private:
    [[nodiscard]] static bool slot_in_range(int slot);

    std::filesystem::path dir_;
    Diagnostics* log_;
    std::optional<GameState> pending_restore_;
};

} // namespace pac::core
