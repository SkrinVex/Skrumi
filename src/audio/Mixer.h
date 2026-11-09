#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
namespace skrumi::audio {
class Mixer {
public:
    // Offline mode is for deterministic renderer tests, never an application fallback.
    explicit Mixer(bool offline = false);
    ~Mixer();
    int play(const std::string& path, int channel, double volume, bool loop);
    int playMemory(std::vector<uint8_t> bytes, int channel, double volume, bool loop);
    void stop(int handle);
    void stopAll();
    void pause(int handle, bool paused);
    bool playing(int handle);
    void volume(int handle, double volume);
    void pan(int handle, double pan);
    void channelVolume(int channel, double volume);
    void masterVolume(double volume);
    void fadeOut(int handle, int milliseconds);
    void render(float* samples, size_t frames);
private:
    struct State;
    std::unique_ptr<State> state_;
};
const char* licenses();
}
