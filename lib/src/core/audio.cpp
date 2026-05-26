#include "engine/core/audio.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/settings.hpp"

#include <SFML/Audio/SoundBuffer.hpp>

namespace pac::core {

MusicPlayer::MusicPlayer(ResourceCache& resources, Diagnostics& log)
    : resources_(resources), log_(log) {}

void MusicPlayer::play(const std::string& logical, bool loop) {
    // sf::Music streams from the buffer we pass in, so the buffer must outlive
    // the music (a requirement of openFromMemory). ResourceCache::persistent_bytes
    // caches the buffer with a stable address — the same path works for both the
    // loose-files and packed backends.
    const std::vector<std::byte>* bytes = nullptr;
    try {
        bytes = resources_.persistent_bytes(logical);
    } catch (const std::exception& e) {
        log_.warn(std::string("music: ") + e.what());
        return;
    }
    if (!bytes || bytes->empty()) {
        log_.warn("music: could not load '" + logical + "'");
        return;
    }
    if (!music_.openFromMemory(bytes->data(), bytes->size())) {
        log_.warn("music: could not decode '" + logical + "'");
        return;
    }
    music_.setLoop(loop);
    music_.setVolume(volume_ * 100.0f);
    music_.play();
}

void MusicPlayer::stop() {
    music_.stop();
}

void MusicPlayer::set_volume(float volume01) {
    volume_ = volume01;
    music_.setVolume(volume_ * 100.0f);
}

SoundPlayer::SoundPlayer(ResourceCache& resources, Diagnostics& log)
    : resources_(resources), log_(log) {}

void SoundPlayer::play(const std::string& logical) {
    const sf::SoundBuffer* buffer = nullptr;
    try {
        buffer = &resources_.sound_buffer(logical);
    } catch (const std::exception& e) {
        log_.warn(std::string("sound: ") + e.what());
        return;
    }

    sf::Sound* voice = nullptr;
    for (sf::Sound& s : voices_) {
        if (s.getStatus() == sf::Sound::Stopped) {
            voice = &s;
            break;
        }
    }
    if (!voice) {
        if (voices_.size() >= kMaxVoices) {
            return; // all voices busy; drop this one
        }
        voice = &voices_.emplace_back();
    }
    voice->setBuffer(*buffer);
    voice->setVolume(volume_ * 100.0f);
    voice->play();
}

void SoundPlayer::stop_all() {
    for (sf::Sound& s : voices_) {
        s.stop();
    }
}

void SoundPlayer::set_volume(float volume01) {
    volume_ = volume01;
    for (sf::Sound& s : voices_) {
        s.setVolume(volume_ * 100.0f);
    }
}

AudioServices::AudioServices(ResourceCache& resources, Diagnostics& log, const Settings& settings)
    : music(resources, log), sfx(resources, log) {
    apply_settings(settings);
}

void AudioServices::apply_settings(const Settings& settings) {
    music.set_volume(settings.audio.music_volume);
    sfx.set_volume(settings.audio.sfx_volume);
}

} // namespace pac::core
