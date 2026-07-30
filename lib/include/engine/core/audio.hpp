#pragma once

#include <SFML/Audio/Music.hpp>
#include <SFML/Audio/Sound.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace pac::core {

class ResourceCache;
class Settings;
class Diagnostics;

namespace detail {

struct MusicCrossfadeGains {
    float outgoing = 1.0f;
    float incoming = 0.0f;
};

/// Equal-power gains for a clamped 0..1 crossfade progress. Kept pure so the
/// envelope is headless-testable without starting an audio device.
[[nodiscard]] MusicCrossfadeGains music_crossfade_gains(float progress);

} // namespace detail

/// Two-deck streamed background music. Volume is 0..1 (mapped to SFML's 0..100).
/// Tracks stream from stable ResourceCache memory in loose and packed builds.
class MusicPlayer {
public:
    MusicPlayer(ResourceCache& resources, Diagnostics& log);

    void play(const std::string& logical, bool loop = true);
    /// Start `logical` on the idle deck and equal-power fade from the active deck.
    /// `preserve_offset` aligns variations of the same composition by carrying the
    /// outgoing playback position modulo the incoming track's duration.
    [[nodiscard]] bool crossfade(const std::string& logical,
                                 float fade_seconds = 2.5f,
                                 bool preserve_offset = false,
                                 bool loop = true);
    void update(float delta_seconds);
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
    bool open(int deck, const std::string& logical, bool loop);
    void cancel_fade();
    void apply_volumes();

    ResourceCache& resources_;
    Diagnostics& log_;
    std::array<sf::Music, 2> decks_;
    int active_ = 0;
    int incoming_ = -1;
    float volume_ = 1.0f;
    float elapsed_ = 0.0f;
    float fade_duration_ = 2.5f;
    bool fading_ = false;
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
    void update(float delta_seconds) { music.update(delta_seconds); }
};

} // namespace pac::core
