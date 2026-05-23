#pragma once

#include <filesystem>
#include <string>

namespace pac::core {

class Settings;
class Diagnostics;

/// Overlay settings parsed from `yaml_text` onto `settings`. Only keys present in
/// the document are touched, so absent keys keep their current value (the manifest
/// defaults applied before loading) — this is how "manifest defaults < user
/// settings" precedence is realized. Tolerant by design: unknown keys are ignored
/// and the function never throws, so a partially-written file still loads.
/// Returns false (and leaves `settings` unchanged) only when the text is not a
/// YAML mapping. `clamp()` is applied to the result.
bool parse_settings_into(const std::string& yaml_text, Settings& settings);

/// Serialize `settings` to the on-disk YAML form (carries a `version:` int).
std::string serialize_settings(const Settings& settings);

/// Reads/writes player settings at a fixed per-user path. Persistence is best
/// effort: a missing file on first run is normal (defaults are kept) and any I/O
/// or parse failure is logged, never fatal — settings are a convenience, not a
/// required resource.
class SettingsStore {
public:
    SettingsStore(std::filesystem::path path, Diagnostics& log);

    /// Overlay the on-disk settings onto `settings` if the file exists. A missing
    /// file is silent (first run); a corrupt file logs a warning and keeps the
    /// passed-in (default) values.
    void load(Settings& settings) const;

    /// Write `settings` to disk. Returns true on success; logs and returns false
    /// on any I/O failure.
    bool save(const Settings& settings) const;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
    Diagnostics& log_;
};

} // namespace pac::core
