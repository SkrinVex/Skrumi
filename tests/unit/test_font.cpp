#include "../../src/graphics/Font.h"
#include "../../src/graphics/Graphics.h"
#include <fstream>
#include <iterator>
#include <iostream>
#include <climits>
#include <algorithm>
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL " #c << ':' << __LINE__ << '\n'; return 1; } } while (0)
template<class F> bool rejects(F action) { try { action(); } catch (const std::exception&) { return true; } return false; }
int main(int argc, char** argv) {
    using namespace skrumi::graphics;
    CHECK(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    CHECK(input.good());
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    Fonts fonts;
    CHECK(rejects([&] { fonts.load("invalid font"); }));
    const int handle = fonts.load(bytes);
    auto& font = fonts.get(handle);
    CHECK(font.width("", 16) == 0);
    CHECK(font.width("WWW", 16) > font.width("iii", 16));
    CHECK(font.width("Hello\ni", 16) == font.width("Hello", 16));
    CHECK(font.width("Hello", 32) > font.width("Hello", 16));
    CHECK(rejects([&] { font.width("x", 0); }));
    Surface surface(128, 64);
    surface.clear(0);
    font.draw(surface, 0, 0, "Skrumi", 24, 0xffffff);
    int changed = 0, smooth = 0;
    for (auto pixel : surface.pixels()) { changed += pixel != 0; smooth += pixel != 0 && pixel != 0xffffff; }
    CHECK(changed > 100 && smooth > 50);
    surface.clear(0);
    font.draw(surface, 0, 0, "Ж", 24, 0xffffff);
    auto cyrillic = surface.pixels();
    surface.clear(0);
    font.draw(surface, 0, 0, "?", 24, 0xffffff);
    CHECK(surface.pixels() != cyrillic);
    surface.clear(0); surface.pushClip(10, 10, 20, 20);
    font.draw(surface, 0, 0, "Skrumi", 24, 0xffffff);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 128; ++x)
        if (x < 10 || x >= 30 || y < 10 || y >= 30) CHECK(surface.pixels()[y*128+x] == 0);
    auto before = surface.pixels();
    font.draw(surface, INT_MAX, INT_MIN, "edge", 24, 0xffffff);
    CHECK(surface.pixels() == before);
    fonts.release(handle);
    CHECK(rejects([&] { fonts.get(handle); }));
    CHECK(rejects([&] { fonts.release(handle); }));
    const int body = fonts.load(bytes), fallback = fonts.load(bytes);
    const int chain = fonts.withFallback(body, fallback);
    const int expectedWidth = fonts.get(body).width("Привет", 24);
    CHECK(fonts.get(chain).supports(U'Ж'));
    CHECK(!fonts.get(chain).supports(U'\U0010FFFF'));
    fonts.release(body); fonts.release(fallback);
    CHECK(fonts.get(chain).width("Привет", 24) == expectedWidth);
    surface.resetClip(); surface.clear(0);
    fonts.get(chain).draw(surface, 0, 0, "A", 24, 0xffffff, 0);
    CHECK(std::all_of(surface.pixels().begin(), surface.pixels().end(), [](auto p) { return p == 0; }));
    fonts.get(chain).draw(surface, 0, 0, "A", 24, 0xffffff, 128);
    CHECK(std::any_of(surface.pixels().begin(), surface.pixels().end(), [](auto p) { return p > 0; }));
    for (auto p : surface.pixels()) CHECK((p & 255) <= 128);
    fonts.release(chain);
    std::cout << "FONT_OK\n";
}
