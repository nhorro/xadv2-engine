#pragma once

#include <SFML/Audio/Music.hpp>
#include <SFML/Audio/Sound.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <random>
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

/// Equal-power gain for a sound-effect fade-out. Progress is clamped to 0..1;
/// invalid values start at full gain.
[[nodiscard]] float sound_fade_gain(float progress);

} // namespace detail

/// Restorable description of the currently selected music cue. Ambient audio
/// and sound effects are separate services and are intentionally absent.
struct MusicState {
    bool playing = false;
    std::string logical;
    bool loop = true;
    float gain = 1.0f;
    float offset_seconds = 0.0f;
};

/// Two-deck streamed background music. Volume is 0..1 (mapped to SFML's 0..100).
/// Tracks stream from stable ResourceCache memory in loose and packed builds.
class MusicPlayer {
public:
    MusicPlayer(ResourceCache& resources, Diagnostics& log);

    /// `gain01` is a cue-local multiplier over the user's configured music
    /// volume; it lets quiet score beds coexist with full-level title music.
    void play(const std::string& logical, bool loop = true, float gain01 = 1.0f);
    /// Start `logical` on the idle deck and equal-power fade from the active deck.
    /// `preserve_offset` aligns variations of the same composition by carrying the
    /// outgoing playback position modulo the incoming track's duration.
    [[nodiscard]] bool crossfade(const std::string& logical,
                                 float fade_seconds = 2.5f,
                                 bool preserve_offset = false,
                                 bool loop = true,
                                 float gain01 = 1.0f);
    /// Fade the active track to silence, then stop its stream. Unlike
    /// `set_volume`, this does not alter the user's configured music volume.
    void fade_out(float fade_seconds = 2.5f);
    void update(float delta_seconds);
    void pause();
    void resume();
    void stop();
    void set_volume(float volume01);

    /// Capture/restore a cue around a temporary musical overlay (for example a
    /// document close-up). Restoring a silent state fades the temporary cue out;
    /// restoring a playing state crossfades back at its captured offset.
    [[nodiscard]] MusicState capture_state() const;
    [[nodiscard]] bool restore_state(const MusicState& state, float fade_seconds = 2.5f);

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
    [[nodiscard]] bool deck_active(int deck) const;
    void start_deck(int deck);
    void stop_deck(int deck);
    void cancel_fade();
    void apply_volumes();

    ResourceCache& resources_;
    Diagnostics& log_;
    std::array<sf::Music, 2> decks_;
    std::array<std::string, 2> logical_;
    std::array<bool, 2> loops_{true, true};
    std::array<float, 2> deck_gains_{1.0f, 1.0f};
    int active_ = 0;
    int incoming_ = -1;
    float volume_ = 1.0f;
    float elapsed_ = 0.0f;
    float fade_duration_ = 2.5f;
    bool fading_ = false;
    bool stopping_ = false;
    bool paused_ = false;
    std::array<bool, 2> resume_decks_{false, false};
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
    /// Stop every active instance of `logical`. A positive duration fades each
    /// matching voice first; zero preserves the immediate-stop behavior.
    void stop(const std::string& logical, float fade_seconds = 0.0f);
    void stop_all(float fade_seconds = 0.0f);
    void update(float delta_seconds);
    void pause();
    void resume();
    void set_volume(float volume01);

private:
    struct Voice {
        sf::Sound sound;
        std::string logical;
        float gain = 1.0f;
        float fade_start = 1.0f;
        float fade_elapsed = 0.0f;
        float fade_duration = 0.0f;
        bool fading = false;
        bool resume_on_unpause = false;
    };

    void stop_voice(Voice& voice, float fade_seconds);
    void apply_voice_volume(Voice& voice);

    static constexpr std::size_t kMaxVoices = 16;
    ResourceCache& resources_;
    Diagnostics& log_;
    std::vector<Voice> voices_;
    float volume_ = 1.0f;
    bool paused_ = false;
};

/// A single non-overlapping native-language voice channel. Spoken lines are
/// separate from effects so dismissing a subtitle stops only its matching voice.
class VoicePlayer {
public:
    VoicePlayer(ResourceCache& resources, Diagnostics& log);

    /// Play a decoded voice resource and return its duration. Returns nullopt
    /// when speech is disabled or the resource cannot be loaded.
    [[nodiscard]] std::optional<float> play(const std::string& logical);
    void stop();
    void pause();
    void resume();
    void set_enabled(bool enabled);
    void set_volume(float volume01);
    [[nodiscard]] bool is_playing() const;

private:
    void apply_volume();

    ResourceCache& resources_;
    Diagnostics& log_;
    sf::Sound sound_;
    float volume_ = 1.0f;
    bool enabled_ = true;
    bool paused_ = false;
    bool resume_on_unpause_ = false;
};

/// Inclusive floating-point range used by randomly scheduled ambience layers.
struct AmbienceRange {
    float min = 0.0f;
    float max = 0.0f;
};

/// The continuously streamed foundation of a room ambience. Only one base is
/// needed for the common case (city, wind, machinery); room changes keep the
/// stream alive when `sound` is unchanged and only glide its gain.
struct AmbienceBaseLayer {
    std::string sound;
    float volume = 1.0f;
};

/// A layer which chooses one of `sounds` after a random delay, with independently
/// randomized volume and stereo pan for each occurrence.
struct AmbienceRandomLayer {
    std::string id;
    std::vector<std::string> sounds;
    AmbienceRange delay{5.0f, 15.0f};
    AmbienceRange volume{1.0f, 1.0f};
    AmbienceRange pan{-1.0f, 1.0f};
};

/// Complete ambience requested by a room. `transition` controls both crossfades
/// between different bases and gain glides for the same persistent base.
struct AmbienceDefinition {
    float transition = 2.5f;
    AmbienceBaseLayer base;
    std::vector<AmbienceRandomLayer> random;
};

/// Persistent room ambience: a streamed base loop plus scheduled, pannable
/// one-shots. It deliberately lives beside music rather than inside RoomScene so
/// an identical base survives room destruction and re-entry without restarting.
class AmbiencePlayer {
public:
    AmbiencePlayer(ResourceCache& resources, Diagnostics& log, SoundPlayer& sfx);

    void configure(const AmbienceDefinition& definition);
    void
    set_base(const std::string& logical, float volume01 = 1.0f, float transition_seconds = 2.5f);
    void set_base_volume(float volume01, float transition_seconds = 1.0f);
    void stop(float transition_seconds = 2.5f);
    void update(float delta_seconds);
    void pause();
    void resume();
    void set_volume(float volume01);

    /// Runtime controls for YAML-declared random layers. Volume is a multiplier
    /// over the authored random range; both return false for an unknown id.
    [[nodiscard]] bool set_random_layer_enabled(const std::string& id, bool enabled);
    [[nodiscard]] bool set_random_layer_volume(const std::string& id, float volume01);

    [[nodiscard]] const std::string& base_sound() const;

private:
    enum class Transition { NONE, VOLUME, CROSSFADE, STOP };
    struct RuntimeRandomLayer {
        AmbienceRandomLayer definition;
        float remaining = 0.0f;
        float volume_scale = 1.0f;
        bool enabled = true;
    };

    bool open(int deck, const std::string& logical);
    [[nodiscard]] bool deck_active(int deck) const;
    void start_deck(int deck);
    void stop_deck(int deck);
    void cancel_transition();
    void apply_loop_volumes();
    void update_loop(float delta_seconds);
    float random_in(AmbienceRange range);
    float next_delay(const AmbienceRandomLayer& layer);

    ResourceCache& resources_;
    Diagnostics& log_;
    SoundPlayer& sfx_;
    std::array<sf::Music, 2> decks_;
    std::array<std::string, 2> logical_;
    int active_ = 0;
    int incoming_ = -1;
    float global_volume_ = 1.0f;
    float current_gain_ = 0.0f;
    float start_gain_ = 0.0f;
    float target_gain_ = 0.0f;
    float elapsed_ = 0.0f;
    float transition_duration_ = 0.0f;
    Transition transition_ = Transition::NONE;
    std::vector<RuntimeRandomLayer> random_layers_;
    std::mt19937 random_;
    bool paused_ = false;
    std::array<bool, 2> resume_decks_{false, false};
};

/// Bundle of the global audio services, with volumes driven by Settings.
struct AudioServices {
    MusicPlayer music;
    SoundPlayer sfx;
    AmbiencePlayer ambience;
    VoicePlayer voice;

    AudioServices(ResourceCache& resources, Diagnostics& log, const Settings& settings);
    void apply_settings(const Settings& settings);
    void pause();
    void resume();
    [[nodiscard]] bool paused() const { return paused_; }
    void update(float delta_seconds) {
        music.update(delta_seconds);
        sfx.update(delta_seconds);
        ambience.update(delta_seconds);
    }

private:
    bool paused_ = false;
};

} // namespace pac::core
