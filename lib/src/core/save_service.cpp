#include "engine/core/save_service.hpp"

#include "engine/core/diagnostics.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace pac::core {

namespace {

constexpr int kSupportedSaveVersion = 1;

std::string slot_filename(int slot) {
    return "slot_" + std::to_string(slot) + ".yaml";
}

std::string thumbnail_filename(int slot) {
    return "slot_" + std::to_string(slot) + ".thumb.png";
}

// --- StateValue codec --------------------------------------------------------
//
// YAML's implicit type detection turns unquoted `true` / `42` into bool / int,
// so the round-trip for a string that happens to look like a bool or number
// would be lossy. Workaround: always emit strings double-quoted, and inspect
// the original scalar style on read.

void emit_state_value(YAML::Emitter& out, const StateValue& v) {
    std::visit(
        [&](auto&& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::string>) {
                out << YAML::DoubleQuoted << x;
            } else if constexpr (std::is_same_v<T, bool>) {
                out << x; // emits true/false
            } else {
                out << x; // double
            }
        },
        v);
}

StateValue decode_state_value(const YAML::Node& node) {
    // A quoted scalar is always a string — we emit strings DoubleQuoted for
    // exactly this reason.
    const bool quoted = node.Type() == YAML::NodeType::Scalar &&
                        (node.Style() == YAML::EmitterStyle::Default ? false : true);
    // yaml-cpp's Style() returns Default for plain scalars; quoted scalars
    // come back as either DoubleQuoted or SingleQuoted in 0.8 (though some
    // versions normalize). To be robust, also inspect the explicit tag.
    if (quoted || node.Tag() == "tag:yaml.org,2002:str" || node.Tag() == "!") {
        return node.as<std::string>();
    }
    // Try bool, then number, then fall back to string.
    if (node.IsScalar()) {
        const std::string& raw = node.Scalar();
        if (raw == "true" || raw == "True" || raw == "TRUE") {
            return true;
        }
        if (raw == "false" || raw == "False" || raw == "FALSE") {
            return false;
        }
        try {
            return node.as<double>();
        } catch (const YAML::Exception&) {
            // not a number
        }
    }
    return node.as<std::string>();
}

void emit_string_value_map(YAML::Emitter& out, const std::map<std::string, StateValue>& m) {
    out << YAML::BeginMap;
    for (const auto& [k, v] : m) {
        out << YAML::Key << YAML::DoubleQuoted << k << YAML::Value;
        emit_state_value(out, v);
    }
    out << YAML::EndMap;
}

std::map<std::string, StateValue> decode_string_value_map(const YAML::Node& node) {
    std::map<std::string, StateValue> out;
    if (!node || !node.IsMap()) {
        return out;
    }
    for (const auto& kv : node) {
        out.emplace(kv.first.as<std::string>(), decode_state_value(kv.second));
    }
    return out;
}

void emit_string_bool_map(YAML::Emitter& out, const std::map<std::string, bool>& m) {
    out << YAML::BeginMap;
    for (const auto& [k, v] : m) {
        out << YAML::Key << YAML::DoubleQuoted << k << YAML::Value << v;
    }
    out << YAML::EndMap;
}

std::map<std::string, bool> decode_string_bool_map(const YAML::Node& node) {
    std::map<std::string, bool> out;
    if (!node || !node.IsMap()) {
        return out;
    }
    for (const auto& kv : node) {
        out.emplace(kv.first.as<std::string>(), kv.second.as<bool>());
    }
    return out;
}

void emit_room_bool_map(YAML::Emitter& out,
                        const std::map<std::string, std::map<std::string, bool>>& m) {
    out << YAML::BeginMap;
    for (const auto& [room_id, inner] : m) {
        out << YAML::Key << YAML::DoubleQuoted << room_id << YAML::Value;
        emit_string_bool_map(out, inner);
    }
    out << YAML::EndMap;
}

std::map<std::string, std::map<std::string, bool>> decode_room_bool_map(const YAML::Node& node) {
    std::map<std::string, std::map<std::string, bool>> out;
    if (!node || !node.IsMap()) {
        return out;
    }
    for (const auto& kv : node) {
        out.emplace(kv.first.as<std::string>(), decode_string_bool_map(kv.second));
    }
    return out;
}

void emit_game_state(YAML::Emitter& out, const GameState& s) {
    out << YAML::BeginMap;
    out << YAML::Key << "save_version" << YAML::Value << s.save_version;
    out << YAML::Key << "description" << YAML::Value << YAML::DoubleQuoted << s.description;
    out << YAML::Key << "saved_at" << YAML::Value << s.saved_at;
    out << YAML::Key << "current_scene_id" << YAML::Value << s.current_scene_id;
    out << YAML::Key << "chapter_id" << YAML::Value << s.chapter_id;

    out << YAML::Key << "room_view" << YAML::Value;
    out << YAML::BeginMap;
    out << YAML::Key << "current_room_id" << YAML::Value << s.room_view.current_room_id;
    out << YAML::Key << "player" << YAML::Value;
    out << YAML::BeginMap;
    out << YAML::Key << "x" << YAML::Value << s.room_view.player.x;
    out << YAML::Key << "y" << YAML::Value << s.room_view.player.y;
    out << YAML::Key << "facing" << YAML::Value << YAML::DoubleQuoted << s.room_view.player.facing;
    out << YAML::Key << "appearance_id" << YAML::Value << YAML::DoubleQuoted
        << s.room_view.player.appearance_id;
    out << YAML::EndMap;
    out << YAML::EndMap;

    out << YAML::Key << "inventory" << YAML::Value;
    out << YAML::BeginSeq;
    for (const std::string& id : s.inventory) {
        out << YAML::DoubleQuoted << id;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "case_terms" << YAML::Value;
    out << YAML::BeginSeq;
    for (const std::string& id : s.case_terms) {
        out << YAML::DoubleQuoted << id;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "global_state" << YAML::Value;
    emit_string_value_map(out, s.global_state);

    out << YAML::Key << "room_state" << YAML::Value;
    out << YAML::BeginMap;
    for (const auto& [room_id, kv_map] : s.room_state) {
        out << YAML::Key << YAML::DoubleQuoted << room_id << YAML::Value;
        emit_string_value_map(out, kv_map);
    }
    out << YAML::EndMap;

    out << YAML::Key << "region_states" << YAML::Value;
    out << YAML::BeginMap;
    for (const auto& [room_id, regions] : s.region_states) {
        out << YAML::Key << YAML::DoubleQuoted << room_id << YAML::Value;
        out << YAML::BeginMap;
        for (const auto& [region_id, state_id] : regions) {
            out << YAML::Key << YAML::DoubleQuoted << region_id << YAML::Value << YAML::DoubleQuoted
                << state_id;
        }
        out << YAML::EndMap;
    }
    out << YAML::EndMap;

    out << YAML::Key << "hotspot_enabled" << YAML::Value;
    emit_room_bool_map(out, s.hotspot_enabled);
    out << YAML::Key << "object_visible" << YAML::Value;
    emit_room_bool_map(out, s.object_visible);
    out << YAML::Key << "layer_visible" << YAML::Value;
    emit_room_bool_map(out, s.layer_visible);
    out << YAML::Key << "obstacle_enabled" << YAML::Value;
    emit_room_bool_map(out, s.obstacle_enabled);

    out << YAML::EndMap;
}

GameState decode_game_state(const YAML::Node& root) {
    GameState s;
    s.save_version = root["save_version"] ? root["save_version"].as<int>() : 0;
    s.description = root["description"] ? root["description"].as<std::string>() : std::string();
    s.saved_at = root["saved_at"] ? root["saved_at"].as<std::int64_t>() : 0;
    s.current_scene_id =
        root["current_scene_id"] ? root["current_scene_id"].as<std::string>() : std::string();
    s.chapter_id = root["chapter_id"] ? root["chapter_id"].as<std::string>() : std::string();

    if (const YAML::Node rv = root["room_view"]) {
        s.room_view.current_room_id =
            rv["current_room_id"] ? rv["current_room_id"].as<std::string>() : std::string();
        if (const YAML::Node p = rv["player"]) {
            s.room_view.player.x = p["x"] ? p["x"].as<float>() : 0.0f;
            s.room_view.player.y = p["y"] ? p["y"].as<float>() : 0.0f;
            s.room_view.player.facing = p["facing"] ? p["facing"].as<std::string>() : "down";
            s.room_view.player.appearance_id =
                p["appearance_id"] ? p["appearance_id"].as<std::string>() : std::string();
        }
    }

    if (const YAML::Node inv = root["inventory"]; inv && inv.IsSequence()) {
        for (const auto& item : inv) {
            s.inventory.push_back(item.as<std::string>());
        }
    }

    if (const YAML::Node terms = root["case_terms"]; terms && terms.IsSequence()) {
        for (const auto& term : terms) {
            s.case_terms.push_back(term.as<std::string>());
        }
    }

    s.global_state = decode_string_value_map(root["global_state"]);

    if (const YAML::Node rs = root["room_state"]; rs && rs.IsMap()) {
        for (const auto& kv : rs) {
            s.room_state.emplace(kv.first.as<std::string>(), decode_string_value_map(kv.second));
        }
    }

    if (const YAML::Node regs = root["region_states"]; regs && regs.IsMap()) {
        for (const auto& kv : regs) {
            const std::string room_id = kv.first.as<std::string>();
            auto& inner = s.region_states[room_id];
            if (kv.second.IsMap()) {
                for (const auto& rkv : kv.second) {
                    inner.emplace(rkv.first.as<std::string>(), rkv.second.as<std::string>());
                }
            }
        }
    }

    s.hotspot_enabled = decode_room_bool_map(root["hotspot_enabled"]);
    s.object_visible = decode_room_bool_map(root["object_visible"]);
    s.layer_visible = decode_room_bool_map(root["layer_visible"]);
    s.obstacle_enabled = decode_room_bool_map(root["obstacle_enabled"]);

    return s;
}

} // namespace

SaveService::SaveService(std::filesystem::path dir, Diagnostics& log)
    : dir_(std::move(dir)), log_(&log) {}

bool SaveService::slot_in_range(int slot) {
    return slot >= 0 && slot < kSlotCount;
}

std::filesystem::path SaveService::slot_path(int slot) const {
    return dir_ / slot_filename(slot);
}

std::filesystem::path SaveService::thumbnail_path(int slot) const {
    return dir_ / thumbnail_filename(slot);
}

bool SaveService::slot_exists(int slot) const {
    if (!slot_in_range(slot)) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(slot_path(slot), ec);
}

bool SaveService::slot_has_thumbnail(int slot) const {
    if (!slot_in_range(slot)) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(thumbnail_path(slot), ec);
}

bool SaveService::save(int slot, const GameState& state, const sf::Image* thumbnail) {
    if (!slot_in_range(slot)) {
        log_->error("SaveService::save: slot " + std::to_string(slot) + " out of range");
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) {
        log_->error("SaveService::save: cannot create '" + dir_.string() + "': " + ec.message());
        return false;
    }

    // Stamp `saved_at` with the current wall-clock so the UI has a stable
    // timestamp even after a file copy that resets mtime.
    GameState stamped = state;
    stamped.saved_at =
        static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count());

    YAML::Emitter out;
    emit_game_state(out, stamped);
    if (!out.good()) {
        log_->error(std::string("SaveService::save: emitter error: ") + out.GetLastError());
        return false;
    }

    const std::filesystem::path path = slot_path(slot);
    std::ofstream file(path);
    if (!file) {
        log_->error("SaveService::save: cannot open '" + path.string() + "' for writing");
        return false;
    }
    file << out.c_str() << '\n';
    file.close();
    if (!file.good()) {
        return false;
    }

    // Thumbnail sidecar (#119). A failure here is non-fatal: the YAML save is
    // already on disk, so we log and continue. An empty image (the room had no
    // capture yet) silently leaves any existing sidecar in place — clearing
    // it would create a worse UX where saving over a slot drops its preview.
    const std::filesystem::path thumb_path = thumbnail_path(slot);
    if (thumbnail && thumbnail->getSize().x > 0 && thumbnail->getSize().y > 0) {
        if (!thumbnail->saveToFile(thumb_path.string())) {
            log_->warn("SaveService::save: could not write thumbnail '" + thumb_path.string() +
                       "'");
        }
    }
    return true;
}

std::optional<GameState> SaveService::load(int slot) {
    if (!slot_in_range(slot)) {
        log_->error("SaveService::load: slot " + std::to_string(slot) + " out of range");
        return std::nullopt;
    }
    if (!slot_exists(slot)) {
        return std::nullopt;
    }
    const std::filesystem::path path = slot_path(slot);
    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        log_->error("SaveService::load: parse error in '" + path.string() + "': " + e.what());
        return std::nullopt;
    }
    if (!root || !root.IsMap()) {
        log_->error("SaveService::load: '" + path.string() + "' has no top-level map");
        return std::nullopt;
    }
    GameState s;
    try {
        s = decode_game_state(root);
    } catch (const YAML::Exception& e) {
        log_->error("SaveService::load: decode error in '" + path.string() + "': " + e.what());
        return std::nullopt;
    }
    if (s.save_version != kSupportedSaveVersion) {
        log_->error("SaveService::load: '" + path.string() + "' has unsupported save_version " +
                    std::to_string(s.save_version));
        return std::nullopt;
    }
    return s;
}

void SaveService::stage_restore(GameState state) {
    pending_restore_ = std::move(state);
}

std::optional<GameState> SaveService::take_pending_restore() {
    if (!pending_restore_) {
        return std::nullopt;
    }
    GameState s = std::move(*pending_restore_);
    pending_restore_.reset();
    return s;
}

void SaveService::stage_pending_snap(GameState state) {
    pending_snap_ = std::move(state);
}

std::optional<GameState> SaveService::take_pending_snap() {
    if (!pending_snap_) {
        return std::nullopt;
    }
    GameState s = std::move(*pending_snap_);
    pending_snap_.reset();
    return s;
}

void SaveService::stage_pending_thumbnail(sf::Image image) {
    pending_thumbnail_ = std::move(image);
}

sf::Image SaveService::take_pending_thumbnail() {
    sf::Image img = std::move(pending_thumbnail_);
    pending_thumbnail_ = sf::Image();
    return img;
}

void SaveService::clear_staged() {
    pending_restore_.reset();
    pending_snap_.reset();
    pending_thumbnail_ = sf::Image();
}

std::optional<SlotSummary> SaveService::slot_summary(int slot) const {
    if (!slot_exists(slot)) {
        return std::nullopt;
    }
    const std::filesystem::path path = slot_path(slot);
    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        log_->error("SaveService::slot_summary: parse error in '" + path.string() +
                    "': " + e.what());
        return std::nullopt;
    }
    if (!root || !root.IsMap()) {
        return std::nullopt;
    }
    SlotSummary out;
    if (const YAML::Node n = root["description"]) {
        try {
            out.description = n.as<std::string>();
        } catch (const YAML::Exception&) {
        }
    }
    if (const YAML::Node n = root["saved_at"]) {
        try {
            out.saved_at = n.as<std::int64_t>();
        } catch (const YAML::Exception&) {
        }
    }
    return out;
}

std::optional<int> SaveService::latest_slot() const {
    std::optional<int> best;
    std::filesystem::file_time_type best_time{};
    for (int slot = 0; slot < kSlotCount; ++slot) {
        if (!slot_exists(slot)) {
            continue;
        }
        std::error_code ec;
        auto t = std::filesystem::last_write_time(slot_path(slot), ec);
        if (ec) {
            continue;
        }
        if (!best || t > best_time) {
            best = slot;
            best_time = t;
        }
    }
    return best;
}

} // namespace pac::core
