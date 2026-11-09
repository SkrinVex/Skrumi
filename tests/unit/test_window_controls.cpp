#include "../../src/graphics/Graphics.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL " #c << ':' << __LINE__ << '\n'; return 1; } } while (0)
int main() {
#ifndef _WIN32
    if (!std::getenv("DISPLAY")) return 77;
#endif
    using namespace skrumi::graphics;
    Window window(320,160,"Skrumi controls regression");
    for (int i=0;i<100 && !window.focused();++i) { window.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    window.setMouseVisible(false); CHECK(!window.mouseVisible()); window.setMouseVisible(true);
    if (window.focused()) { window.setMouseCaptured(true); window.focusEvent(false); }
    window.setTargetFps(100);
    auto start=std::chrono::steady_clock::now();
    for (int i=0;i<4;++i) window.present();
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count() >= 0.03);
    window.setTargetFps(0);
    auto draw = [&](const std::string& value) { return window.ui().text("input",0,0,300,40,value,1,0xffffff,window); };
    window.ui().setFocus("input");
    std::string text = draw("a👨‍👩‍👧‍👦");
    window.poll(); window.keyEvent(8,true); window.keyEvent(8,false); text=draw(text);
    CHECK(text == "a");
    window.poll(); window.textEvent("e"); window.textEvent("\xcc\x81"); text=draw(text);
    CHECK(text == "aé");
    window.poll(); window.keyEvent(8,true); window.keyEvent(8,false); text=draw(text);
    CHECK(text == "a");
    window.poll(); window.textEvent("\r\n"); text=draw(text); CHECK(text == "a");
    std::cout << "WINDOW_CONTROLS_AND_GRAPHEME_EDIT_OK\n";
}
