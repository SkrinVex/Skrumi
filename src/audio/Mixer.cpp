#include "Mixer.h"
#include "AudioLicenses.h"
#include "skrumi/Platform.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#ifndef __EMSCRIPTEN__
#include <miniaudio.h>
#endif
namespace skrumi::audio {
namespace {
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("Audio Error: " + message); }
void gain(double value) { if (!std::isfinite(value) || value < 0 || value > 1) fail("volume must be 0..1"); }
void channel(int value) { if (value < 0 || value > 2) fail("channel must be SFX (0), MUSIC (1) or UI (2)"); }
#ifndef __EMSCRIPTEN__
void checked(ma_result result) { if (result != MA_SUCCESS) fail(ma_result_description(result)); }
#endif
}
#ifndef __EMSCRIPTEN__
struct Mixer::State {
    ma_engine engine{};
    ma_sound_group groups[3]{};
    bool initialized = false, offline = false;
    int groupCount = 0, next = 1;
    struct Voice {
        ma_sound sound{};
        ma_decoder decoder{};
        bool soundReady = false, decoderReady = false, paused = false;
        std::vector<uint8_t> bytes;
        ~Voice() { if (soundReady) ma_sound_uninit(&sound); if (decoderReady) ma_decoder_uninit(&decoder); }
    };
    std::map<int, std::unique_ptr<Voice>> voices;
    explicit State(bool off) : offline(off) {
        auto config = ma_engine_config_init();
        config.noDevice = off; config.channels = 2; config.sampleRate = 48000;
        checked(ma_engine_init(&config, &engine)); initialized = true;
        try {
            for (auto& group : groups) { checked(ma_sound_group_init(&engine, 0, nullptr, &group)); ++groupCount; }
        } catch (...) { cleanup(); throw; }
    }
    void cleanup() {
        voices.clear();
        for (int i=groupCount; i>0; --i) ma_sound_group_uninit(&groups[i-1]);
        groupCount = 0;
        if (initialized) ma_engine_uninit(&engine);
        initialized = false;
    }
    ~State() { cleanup(); }
    Voice& get(int handle) { auto i = voices.find(handle); if (i == voices.end()) fail("invalid sound handle"); return *i->second; }
    void room() {
        for (auto it=voices.begin(); it!=voices.end();) {
            if (!it->second->paused && (!ma_sound_is_playing(&it->second->sound) || ma_sound_at_end(&it->second->sound))) it=voices.erase(it);
            else ++it;
        }
        if (voices.size() >= 128) fail("128 active sounds; stop unused handles");
        if (next == std::numeric_limits<int>::max()) fail("sound handle limit reached");
    }
    int start(std::unique_ptr<Voice> voice, double volume, bool loop) {
        voice->soundReady = true;
        ma_sound_set_volume(&voice->sound, static_cast<float>(volume));
        ma_sound_set_looping(&voice->sound, loop);
        checked(ma_sound_start(&voice->sound));
        int handle = next++;
        voices.emplace(handle, std::move(voice)); return handle;
    }
};
Mixer::Mixer(bool offline) : state_(std::make_unique<State>(offline)) {}
Mixer::~Mixer() = default;
int Mixer::play(const std::string& path, int lane, double volume, bool loop) {
    channel(lane); gain(volume); state_->room();
    if (path.find('\0') != std::string::npos) fail("path must not contain NUL");
    auto voice = std::make_unique<State::Voice>();
#ifdef _WIN32
    auto wide = platform::pathFromUtf8(path).wstring();
    checked(ma_sound_init_from_file_w(&state_->engine, wide.c_str(), MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                     &state_->groups[lane], nullptr, &voice->sound));
#else
    checked(ma_sound_init_from_file(&state_->engine, path.c_str(), MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                   &state_->groups[lane], nullptr, &voice->sound));
#endif
    return state_->start(std::move(voice), volume, loop);
}
int Mixer::playMemory(std::vector<uint8_t> bytes, int lane, double volume, bool loop) {
    channel(lane); gain(volume); state_->room();
    if (bytes.empty() || bytes.size() > 64 * 1024 * 1024) fail("sound data must contain 1 byte..64 MiB");
    auto voice = std::make_unique<State::Voice>(); voice->bytes = std::move(bytes);
    auto config = ma_decoder_config_init(ma_format_f32, 2, 48000);
    checked(ma_decoder_init_memory(voice->bytes.data(), voice->bytes.size(), &config, &voice->decoder));
    voice->decoderReady = true;
    checked(ma_sound_init_from_data_source(&state_->engine, &voice->decoder, MA_SOUND_FLAG_NO_SPATIALIZATION,
                                          &state_->groups[lane], &voice->sound));
    return state_->start(std::move(voice), volume, loop);
}
void Mixer::stop(int handle) { state_->voices.erase(handle); }
void Mixer::stopAll() { state_->voices.clear(); }
void Mixer::pause(int handle, bool paused) {
    auto& voice = state_->get(handle);
    checked(paused ? ma_sound_stop(&voice.sound) : ma_sound_start(&voice.sound)); voice.paused = paused;
}
bool Mixer::playing(int handle) {
    auto it = state_->voices.find(handle);
    return it != state_->voices.end() && ma_sound_is_playing(&it->second->sound);
}
void Mixer::volume(int handle, double value) { gain(value); ma_sound_set_volume(&state_->get(handle).sound, static_cast<float>(value)); }
void Mixer::pan(int handle, double value) {
    if (!std::isfinite(value) || value < -1 || value > 1) fail("pan must be -1..1");
    ma_sound_set_pan(&state_->get(handle).sound, static_cast<float>(value));
}
void Mixer::channelVolume(int lane, double value) { channel(lane); gain(value); ma_sound_group_set_volume(&state_->groups[lane], static_cast<float>(value)); }
void Mixer::masterVolume(double value) { gain(value); checked(ma_engine_set_volume(&state_->engine, static_cast<float>(value))); }
void Mixer::fadeOut(int handle, int milliseconds) {
    if (milliseconds < 0 || milliseconds > 86400000) fail("fade duration must be 0..86400000 ms");
    auto& sound = state_->get(handle).sound;
    ma_sound_set_fade_in_milliseconds(&sound, -1, 0, static_cast<ma_uint64>(milliseconds));
    ma_sound_set_stop_time_in_pcm_frames(&sound, ma_engine_get_time_in_pcm_frames(&state_->engine) + static_cast<ma_uint64>(milliseconds) * 48);
}
void Mixer::render(float* samples, size_t frames) {
    if (!state_->offline) fail("render requires an offline mixer");
    checked(ma_engine_read_pcm_frames(&state_->engine, samples, frames, nullptr));
}
#else
struct Mixer::State {};
Mixer::Mixer(bool) { fail("the native audio mixer is unavailable in the browser"); }
Mixer::~Mixer() = default;
int Mixer::play(const std::string&, int, double, bool) { return 0; }
int Mixer::playMemory(std::vector<uint8_t>, int, double, bool) { return 0; }
void Mixer::stop(int) {} void Mixer::stopAll() {} void Mixer::pause(int, bool) {}
bool Mixer::playing(int) { return false; }
void Mixer::volume(int, double) {} void Mixer::pan(int, double) {}
void Mixer::channelVolume(int, double) {} void Mixer::masterVolume(double) {}
void Mixer::fadeOut(int, int) {} void Mixer::render(float*, size_t) {}
#endif
const char* licenses() { return skrumiAudioLicense(); }
}
