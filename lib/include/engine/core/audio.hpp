#pragma once

#include <SFML/Audio/Music.hpp>
#include <SFML/Audio/Sound.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace pac::core {

class ResourceCache;
class Settings;
class Diagnostics;

/// One streamed background track at a time. Volume is 0..1 (mapped to SFML's
/// 0..100). Music streams from a host file, so it needs a filesystem-backed cache.
class MusicPlayer {
public:
    MusicPlayer(ResourceCache& resources, Diagnostics& log);

    void play(const std::string& logical, bool loop = true);
    void stop();
    void set_volume(float volume01);

    /// Current playback offset of the active track in seconds. Returns 0 when
    /// nothing is playing. Used by timed cutscenes (issue #116) to keep slide
    /// transitions in sync with a narrated audio track when the engine
    /// stutters or pauses.
    [[nodiscard]] float playing_offset_seconds() const;

    /// True while the underlying `sf::Music` is in the Playing state. Lets
    /// callers detect track end without polling for offset == duration.
    [[nodiscard]] bool is_playing() const;

private:
    ResourceCache& resources_;
    Diagnostics& log_;
    sf::Music music_;
    float volume_ = 1.0f;
};

/// Short overlapping sound effects, played from cached sound buffers.
class SoundPlayer {
public:
    SoundPlayer(ResourceCache& resources, Diagnostics& log);

    /// Play a one-shot effect. `volume01` (0..1) scales the global SFX volume for
    /// this voice; `pan` (-1 left .. 0 center .. +1 right) places it in the stereo
    /// field. Panning only affects MONO buffers — a stereo clip plays as-authored.
    /// A simple stereo balance for now; a position-aware/spatial path is design-for.
    void play(const std::string& logical, float volume01 = 1.0f, float pan = 0.0f);
    void stop_all();
    void set_volume(float volume01);

private:
    static constexpr std::size_t kMaxVoices = 16;
    ResourceCache& resources_;
    Diagnostics& log_;
    std::vector<sf::Sound> voices_;
    float volume_ = 1.0f;
};

/// Bundle of the global audio services, with volumes driven by Settings.
struct AudioServices {
    MusicPlayer music;
    SoundPlayer sfx;

    AudioServices(ResourceCache& resources, Diagnostics& log, const Settings& settings);
    void apply_settings(const Settings& settings);
};

} // namespace pac::core
