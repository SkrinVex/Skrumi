// Sound: WAV files and generated tones, played without blocking the program.
// Windows plays through PlaySound (winmm); Linux hands the file to the desktop's
// player (pw-play, paplay or aplay), so a static binary needs no audio library.
#include "Builtin.h"
#include "../../audio/Mixer.h"
#include "skrumi/Platform.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#ifdef __ANDROID__
#include "skrumi/Android.h"
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#else
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace skrumi::runtime {
namespace {

audio::Mixer& mixer(Call& c) {
    auto& slot = c.ctx.getRoot()->mixer;
    if (!slot) slot = std::make_shared<audio::Mixer>();
    return *slot;
}
// 16-bit mono PCM at 44100 Hz: a sine with 10 ms fades so it starts and ends without a click.
std::vector<uint8_t> toneWav(double frequency, long long milliseconds, double volume) {
    const uint32_t rate = 44100;
    uint32_t samples = static_cast<uint32_t>(rate * milliseconds / 1000);
    uint32_t dataBytes = samples * 2;
    std::vector<uint8_t> wav(44 + dataBytes);
    auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) wav[at + i] = static_cast<uint8_t>(v >> (8 * i)); };
    auto put16 = [&](size_t at, uint16_t v) { wav[at] = static_cast<uint8_t>(v); wav[at + 1] = static_cast<uint8_t>(v >> 8); };
    std::memcpy(wav.data(), "RIFF", 4);
    put32(4, 36 + dataBytes);
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);          // PCM
    put16(22, 1);          // mono
    put32(24, rate);
    put32(28, rate * 2);   // bytes per second
    put16(32, 2);          // block align
    put16(34, 16);         // bits per sample
    std::memcpy(wav.data() + 36, "data", 4);
    put32(40, dataBytes);
    const double fade = rate * 0.01;
    for (uint32_t i = 0; i < samples; ++i) {
        double envelope = std::min({1.0, i / fade, (samples - i) / fade});
        double value = std::sin(2 * 3.14159265358979323846 * frequency * i / rate) * volume * envelope;
        put16(44 + size_t(i) * 2, static_cast<uint16_t>(static_cast<int16_t>(std::lround(value * 32767))));
    }
    return wav;
}

#ifdef __ANDROID__
bool playFile(const std::string& path) {
    std::ifstream file(platform::pathFromUtf8(path), std::ios::binary);
    std::vector<uint8_t> wav((std::istreambuf_iterator<char>(file)), {});
    return android::host().playWav(wav);
}
bool playTone(std::vector<uint8_t> wav) { return android::host().playWav(wav); }
void stopAll() { android::host().stopSound(); }
#elif defined(_WIN32)

std::mutex playing;
std::vector<uint8_t> toneBuffer; // PlaySound reads a memory sound while it plays

bool playFile(const std::string& path) {
    std::wstring wide = platform::pathFromUtf8(path).wstring();
    return PlaySoundW(wide.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT) != FALSE;
}

bool playTone(std::vector<uint8_t> wav) {
    std::lock_guard<std::mutex> lock(playing);
    PlaySoundW(nullptr, nullptr, 0); // stop the old tone before its buffer goes away
    toneBuffer = std::move(wav);
    return PlaySoundW(reinterpret_cast<LPCWSTR>(toneBuffer.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT) != FALSE;
}

void stopAll() {
    std::lock_guard<std::mutex> lock(playing);
    PlaySoundW(nullptr, nullptr, 0);
}

#else

struct Playing {
    pid_t pid;
    std::string temporary; // a generated tone's file, removed once its player is done
};
std::mutex playing;
std::vector<Playing> players;

// Players that finished are collected so they leave no zombie processes behind.
void reap() {
    for (auto it = players.begin(); it != players.end();) {
        int status = 0;
        if (waitpid(it->pid, &status, WNOHANG) == 0) {
            ++it;
            continue;
        }
        if (!it->temporary.empty()) std::remove(it->temporary.c_str());
        it = players.erase(it);
    }
}

std::string findPlayer() {
    std::string chosen = platform::configEnv("SKRUMI_SOUND_PLAYER");
    if (!chosen.empty()) return chosen;
    std::string path = platform::getEnvVar("PATH");
    for (const char* candidate : {"pw-play", "paplay", "aplay"}) {
        size_t start = 0;
        while (start <= path.size()) {
            size_t end = path.find(':', start);
            std::string dir = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
            std::string full = (dir.empty() ? "." : dir) + "/" + candidate;
            if (access(full.c_str(), X_OK) == 0) return full;
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    return "";
}

bool spawnPlayer(const std::string& file, const std::string& temporary) {
    std::lock_guard<std::mutex> lock(playing);
    reap();
    std::string player = findPlayer();
    if (player.empty()) {
        if (!temporary.empty()) std::remove(temporary.c_str());
        return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // The player's own messages would land in the program's terminal.
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    std::vector<std::string> words = {player};
    if (player.size() >= 5 && player.compare(player.size() - 5, 5, "aplay") == 0) words.push_back("-q");
    words.push_back(file);
    std::vector<char*> argv;
    for (auto& word : words) argv.push_back(word.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    int result = posix_spawnp(&pid, player.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (result != 0) {
        if (!temporary.empty()) std::remove(temporary.c_str());
        return false;
    }
    players.push_back({pid, temporary});
    return true;
}

bool playFile(const std::string& path) { return spawnPlayer(path, ""); }

bool playTone(std::vector<uint8_t> wav) {
    static std::atomic<unsigned> counter{0};
    std::error_code ignored;
    auto file = std::filesystem::temp_directory_path(ignored) /
                ("skrumi-tone-" + std::to_string(getpid()) + "-" + std::to_string(counter++) + ".wav");
    std::string name = file.string();
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));
        if (!out) return false;
    }
    return spawnPlayer(name, name);
}

void stopAll() {
    std::lock_guard<std::mutex> lock(playing);
    for (const auto& player : players) kill(player.pid, SIGTERM);
    for (const auto& player : players) {
        int status = 0;
        waitpid(player.pid, &status, 0);
        if (!player.temporary.empty()) std::remove(player.temporary.c_str());
    }
    players.clear();
}

#endif

} // namespace

void addSoundBuiltins(std::vector<Builtin>& out) {
    auto add = [&](BuiltinSpec spec, Handler handler) { out.push_back({std::move(spec), handler}); };
    add({"sound_play_ex", "int", {{"string", "path"},{"int", "channel"},{"float", "volume"},{"bool", "loop"}}, 4, false, "sound", "Потоковое воспроизведение WAV/MP3/FLAC/OGG; отдельный handle, канал 0..2, volume 0..1. Ошибка устройства — Audio Error."},
        [](Call& c) { return integer(mixer(c).play(c.text(0), static_cast<int>(c.integer(1)), c.number(2), c.flag(3))); });
    add({"sound_stop_handle", "void", {{"int", "handle"}}, 1, false, "sound", "Остановить handle; повторный вызов безопасен."},
        [](Call& c) { if (c.ctx.getRoot()->mixer) c.ctx.getRoot()->mixer->stop(static_cast<int>(c.integer(0))); return nothing(); });
    add({"sound_pause", "void", {{"int", "handle"},{"bool", "paused"}}, 2, false, "sound", "Пауза или продолжение отдельного звука."},
        [](Call& c) { mixer(c).pause(static_cast<int>(c.integer(0)), c.flag(1)); return nothing(); });
    add({"sound_is_playing", "bool", {{"int", "handle"}}, 1, false, "sound", "Играет ли handle сейчас; false для завершённого или остановленного."},
        [](Call& c) { return boolean(c.ctx.getRoot()->mixer && c.ctx.getRoot()->mixer->playing(static_cast<int>(c.integer(0)))); });
    add({"sound_volume", "void", {{"int", "handle"},{"float", "volume"}}, 2, false, "sound", "Громкость отдельного звука 0..1."},
        [](Call& c) { mixer(c).volume(static_cast<int>(c.integer(0)), c.number(1)); return nothing(); });
    add({"sound_pan", "void", {{"int", "handle"},{"float", "pan"}}, 2, false, "sound", "Панорама -1..1."},
        [](Call& c) { mixer(c).pan(static_cast<int>(c.integer(0)), c.number(1)); return nothing(); });
    add({"sound_channel_volume", "void", {{"int", "channel"},{"float", "volume"}}, 2, false, "sound", "Громкость канала 0..1."},
        [](Call& c) { mixer(c).channelVolume(static_cast<int>(c.integer(0)), c.number(1)); return nothing(); });
    add({"sound_master_volume", "void", {{"float", "volume"}}, 1, false, "sound", "Общая громкость микшера 0..1."},
        [](Call& c) { mixer(c).masterVolume(c.number(0)); return nothing(); });
    add({"sound_fade_out", "void", {{"int", "handle"},{"int", "milliseconds"}}, 2, false, "sound", "Плавно снизить громкость до нуля и остановить звук."},
        [](Call& c) { mixer(c).fadeOut(static_cast<int>(c.integer(0)), static_cast<int>(c.integer(1))); return nothing(); });
    add({"sound_play", "bool", {{"string", "path"}}, 1, false, "sound",
         "Начинает проигрывать звуковой файл WAV и сразу возвращает управление. `false`, если звук не удалось "
         "запустить: нет файла, звуковой системы или проигрывателя."},
        [](Call& c) {
            std::error_code missing;
            if (!std::filesystem::is_regular_file(platform::pathFromUtf8(c.text(0)), missing)) return boolean(false);
#ifdef _WIN32
            try { mixer(c).play(c.text(0), 0, 1, false); return boolean(true); } catch (const std::exception&) { return boolean(false); }
#else
            return boolean(playFile(c.text(0)));
#endif
        });
    add({"sound_tone", "bool", {{"float", "frequency"}, {"int", "milliseconds"}, {"float", "volume"}}, 3, false, "sound",
         "Проигрывает тон: частота 20..20000 Гц, длительность 1..60000 мс, громкость 0..1. Не ждёт окончания."},
        [](Call& c) {
            double frequency = c.number(0), volume = c.number(2);
            long long milliseconds = c.integer(1);
            if (frequency < 20 || frequency > 20000) throw std::runtime_error("Runtime Error: tone frequency must be 20..20000 Hz");
            if (milliseconds < 1 || milliseconds > 60000) throw std::runtime_error("Runtime Error: tone length must be 1..60000 ms");
            if (volume < 0 || volume > 1) throw std::runtime_error("Runtime Error: tone volume must be 0..1");
#ifdef _WIN32
            try { mixer(c).playMemory(toneWav(frequency, milliseconds, volume), 0, 1, false); return boolean(true); } catch (const std::exception&) { return boolean(false); }
#else
            return boolean(playTone(toneWav(frequency, milliseconds, volume)));
#endif
        });
    add({"sound_stop", "void", {}, 0, false, "sound", "Останавливает все звуки, запущенные программой."},
        [](Call& c) {
            if (c.ctx.getRoot()->mixer) c.ctx.getRoot()->mixer->stopAll();
            stopAll();
            return nothing();
        });
}

} // namespace skrumi::runtime
