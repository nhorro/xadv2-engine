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

    void play(const std::string& logical);
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
