#include "../../src/audio/Mixer.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " #x << ":" << __LINE__ << '\n'; return 1; } } while (0)
std::vector<uint8_t> wav() {
    const unsigned n = 48000;
    std::vector<uint8_t> bytes(44+n*2);
    auto put = [&](size_t at, unsigned v, int count) { for (int i=0;i<count;++i) bytes[at+i]=static_cast<uint8_t>(v>>(8*i)); };
    std::memcpy(bytes.data(), "RIFF", 4); put(4,36+n*2,4);
    std::memcpy(bytes.data()+8,"WAVEfmt ",8); put(16,16,4); put(20,1,2); put(22,1,2);
    put(24,48000,4); put(28,96000,4); put(32,2,2); put(34,16,2);
    std::memcpy(bytes.data()+36,"data",4); put(40,n*2,4);
    for (unsigned i=0;i<n;++i) put(44+i*2, static_cast<unsigned>(std::sin(i*0.1)*4000),2);
    return bytes;
}
double energy(const std::vector<float>& audio) { double total=0; for(float x:audio) total += std::abs(x); return total; }
int main() {
    skrumi::audio::Mixer single(true), mixed(true);
    int a = single.playMemory(wav(),0,1,true);
    int b = mixed.playMemory(wav(),0,1,true);
    int c = mixed.playMemory(wav(),1,1,true);
    std::vector<float> one(2048), two(2048);
    single.render(one.data(),1024); mixed.render(two.data(),1024);
    CHECK(energy(one)>1);
    for (size_t i=0;i<one.size();++i) CHECK(std::abs(two[i]-2*one[i])<0.0001);
    CHECK(mixed.playing(b) && mixed.playing(c));
    mixed.stop(b); CHECK(!mixed.playing(b) && mixed.playing(c));
    mixed.pause(c,true); CHECK(!mixed.playing(c));
    mixed.render(two.data(),1024); // Drain resampler/node buffers.
    mixed.render(two.data(),1024); CHECK(energy(two)<0.001);
    mixed.pause(c,false); mixed.render(two.data(),1024); CHECK(energy(two)>1);
    mixed.channelVolume(1,0); mixed.render(two.data(),1024); mixed.render(two.data(),1024); CHECK(energy(two)<0.001);
    mixed.channelVolume(1,1); mixed.fadeOut(c,10); mixed.render(two.data(),1024); mixed.render(two.data(),1024); CHECK(energy(two)<0.001);
    single.stop(a); single.stop(a); single.stopAll(); mixed.stopAll();
    bool rejected=false; try { single.playMemory(wav(),3,1,false); } catch (...) { rejected=true; } CHECK(rejected);
    rejected=false; try { single.masterVolume(-1); } catch (...) { rejected=true; } CHECK(rejected);
    std::cout << "AUDIO_MIXER_OK\n";
}
