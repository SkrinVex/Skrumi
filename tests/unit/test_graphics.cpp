#include "../../src/graphics/Graphics.h"
#include "skrumi/Skrumi.h"
#include <algorithm>
#include <climits>
#include <iostream>
#include <stdexcept>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "FAIL " #condition << ':' << __LINE__ << '\n'; return 1; } } while (0)
int main() {
    using skrumi::graphics::Surface;
    Surface resized(80, 60);
    resized.clear(0x123456);
    resized.rectangle(20, 20, 5, 5, 0xabcdef);
    resized.resize(120, 90);
    CHECK(resized.pixels()[22 * 120 + 22] == 0xabcdef);
    CHECK(resized.pixels()[59 * 120 + 79] == 0x123456);
    CHECK(resized.pixels()[80 * 120 + 110] == 0);
    resized.resize(40, 30);
    CHECK(resized.pixels()[22 * 40 + 22] == 0xabcdef);
    Surface pixels(32, 24);
    pixels.clear(0x102030);
    pixels.rectangle(-2, -2, 4, 4, 0xff0000);
    CHECK(pixels.pixels()[0] == 0xff0000);
    CHECK(pixels.pixels()[1 + 32] == 0xff0000);
    CHECK(pixels.pixels()[2] == 0x102030);
    auto before = pixels.pixels();
    pixels.rectangle(INT_MAX, INT_MAX, INT_MAX, INT_MAX, 0);
    pixels.rectangle(INT_MIN, INT_MIN, 4, 4, 0);
    pixels.circle(INT_MAX, INT_MIN, 8192, 0);
    CHECK(pixels.pixels() == before);
    pixels.circle(16, 12, 3, 0xabcdef);
    CHECK(pixels.pixels()[12 * 32 + 16] == 0xabcdef);
    CHECK(pixels.pixels()[12 * 32 + 19] == 0xabcdef);
    CHECK(pixels.pixels()[12 * 32 + 20] == 0x102030);
    pixels.clear(0);
    pixels.text(0, 0, "Лисий\nЁж", 1, 0xffffff);
    CHECK(std::count(pixels.pixels().begin(), pixels.pixels().end(), 0xffffff) > 30);
    before = pixels.pixels();
    pixels.clear(0);
    pixels.text(0, 0, "ЛИСИЙ\nЁЖ", 1, 0xffffff);
    CHECK(pixels.pixels() != before); // lower case has its own letters
    // Every Latin and Cyrillic lower-case letter has a glyph of its own, not '?'.
    for (const char* letter : {"a", "g", "m", "q", "z", "б", "д", "ж", "л", "ф", "щ", "ы", "ю", "я", "ё"}) {
        Surface a(8, 8), b(8, 8);
        a.text(0, 0, letter, 1, 0xffffff);
        b.text(0, 0, "?", 1, 0xffffff);
        CHECK(a.pixels() != b.pixels());
    }
    {
        Surface latin(8, 8), cyrillic(8, 8);
        latin.text(0, 0, "o", 1, 0xffffff);
        cyrillic.text(0, 0, "о", 1, 0xffffff);
        CHECK(latin.pixels() == cyrillic.pixels()); // Cyrillic о looks like Latin o
    }
    pixels.text(INT_MAX, INT_MAX, "ignored", 32, 0);
    pixels.text(INT_MIN, INT_MIN, "ignored", 1, 0);
    pixels.text(0, 0, "\xf0\x80\x80\x80\xed\xa0\x80\xff", 1, 0xff00ff);
    bool rejected = false;
    try { Surface invalid(4096, 4096); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    rejected = false;
    try { pixels.circle(0, 0, -1, 0); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    rejected = false;
    try { pixels.text(0, 0, "test", 0, 0); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    // Lines, frames, rings and text measurement
    pixels.clear(0);
    pixels.line(0, 0, 31, 23, 0x00ff00);
    CHECK(pixels.pixels()[0] == 0x00ff00 && pixels.pixels()[23 * 32 + 31] == 0x00ff00);
    pixels.line(-100, 5, 100, 5, 0x0000ff);
    CHECK(pixels.pixels()[5 * 32] == 0x0000ff && pixels.pixels()[5 * 32 + 31] == 0x0000ff);
    pixels.clear(0);
    pixels.frame(2, 2, 10, 8, 1, 0xff0000);
    CHECK(pixels.pixels()[2 * 32 + 2] == 0xff0000 && pixels.pixels()[9 * 32 + 11] == 0xff0000);
    CHECK(pixels.pixels()[5 * 32 + 6] == 0);
    pixels.clear(0);
    pixels.ring(16, 12, 6, 1, 0xffffff);
    CHECK(pixels.pixels()[12 * 32 + 22] == 0xffffff && pixels.pixels()[12 * 32 + 16] == 0);
    CHECK(Surface::textWidth("ab", 1) == 11 && Surface::textWidth("ab\nЛисий", 2) == 58 && Surface::textWidth("", 3) == 0);
    // Punctuation used in games and interfaces has its own glyph instead of '?'
    for (const char* symbol : {"%", "\"", "'", "*", "#", "<", ">", "@", "&", "{", "}", "|", "$", "№", "°", "«", "»"}) {
        Surface a(8, 8), b(8, 8);
        a.text(0, 0, symbol, 1, 0xffffff);
        b.text(0, 0, "?", 1, 0xffffff);
        CHECK(a.pixels() != b.pixels());
    }
    // Clip rectangles nest: drawing stays inside the innermost one until it is closed
    pixels.clear(0);
    pixels.pushClip(4, 4, 10, 10);
    pixels.pushClip(8, 0, 32, 32);
    pixels.rectangle(0, 0, 32, 24, 0xffffff);
    pixels.text(0, 6, "WW", 1, 0x00ff00);
    pixels.line(0, 10, 31, 10, 0xff0000);
    CHECK(pixels.pixels()[8 * 32 + 7] == 0 && pixels.pixels()[3 * 32 + 10] == 0 && pixels.pixels()[14 * 32 + 10] == 0);
    CHECK(pixels.pixels()[10 * 32 + 8] == 0xff0000 && pixels.pixels()[10 * 32 + 14] == 0 && pixels.pixels()[10 * 32 + 20] == 0);
    CHECK(pixels.pixels()[5 * 32 + 12] == 0xffffff);
    pixels.popClip();
    pixels.popClip();
    pixels.rectangle(0, 0, 2, 2, 0x123456);
    CHECK(pixels.pixels()[0] == 0x123456);
    bool unbalanced = false;
    try { pixels.popClip(); } catch (const std::runtime_error&) { unbalanced = true; }
    CHECK(unbalanced);
    // Every alpha primitive agrees on source-over, including clipping and alpha 0.
    pixels.clear(0);
    pixels.circle(16, 12, 3, 0xff0000, 128);
    CHECK(pixels.pixels()[12 * 32 + 16] == 0x800000);
    pixels.line(0, 0, 31, 0, 0x00ff00, 128);
    CHECK(pixels.pixels()[0] == 0x008000);
    pixels.ring(16, 12, 5, 1, 0x0000ff, 128);
    CHECK(pixels.pixels()[12 * 32 + 21] == 0x000080);
    before = pixels.pixels();
    pixels.text(0, 0, "transparent", 2, 0xffffff, 0);
    CHECK(before == pixels.pixels());
    CHECK(Surface::hasGlyph(U'★') && Surface::hasGlyph(U'•') && !Surface::hasGlyph(U'🦊'));
    // Pictures: stretched by nearest pixel, their alpha and the opacity mixed in, clipped
    {
        skrumi::graphics::Image picture;
        picture.width = 2;
        picture.height = 1;
        picture.pixels = {0xFFFF0000u, 0x800000FFu}; // opaque red, half-transparent blue
        Surface canvas(8, 4);
        canvas.clear(0x000000);
        canvas.image(picture, 0, 0, 2, 1, 0, 0, 8, 2, 255);
        CHECK(canvas.pixels()[0] == 0xFF0000 && canvas.pixels()[3] == 0xFF0000 && canvas.pixels()[8 + 3] == 0xFF0000);
        CHECK(canvas.pixels()[4] == 0x000080 && canvas.pixels()[2 * 8] == 0);
        canvas.clear(0xFFFFFF);
        canvas.image(picture, 0, 0, 1, 1, 0, 0, 1, 1, 0);
        CHECK(canvas.pixels()[0] == 0xFFFFFF);
        canvas.pushClip(1, 0, 1, 1);
        canvas.image(picture, 0, 0, 1, 1, 0, 0, 4, 1, 255);
        CHECK(canvas.pixels()[0] == 0xFFFFFF && canvas.pixels()[1] == 0xFF0000 && canvas.pixels()[2] == 0xFFFFFF);
        canvas.popClip();
        bool outside = false;
        try { canvas.image(picture, 1, 0, 2, 1, 0, 0, 1, 1, 255); } catch (const std::runtime_error&) { outside = true; }
        CHECK(outside);
    }
    // X11 keysyms a Russian layout sends become Cyrillic text
    using skrumi::graphics::keysymToUnicode;
    CHECK(keysymToUnicode('a') == 'a' && keysymToUnicode('/') == '/' && keysymToUnicode(0xE9) == 0xE9);
    CHECK(keysymToUnicode(0x6CC) == 0x43B && keysymToUnicode(0x6EC) == 0x41B); // л Л
    CHECK(keysymToUnicode(0x6C1) == 0x430 && keysymToUnicode(0x6FF) == 0x42A); // а Ъ
    CHECK(keysymToUnicode(0x6A3) == 0x451 && keysymToUnicode(0x6B3) == 0x401); // ё Ё
    CHECK(keysymToUnicode(0x100263A) == 0x263A && keysymToUnicode(0xFFB5) == '5');
    CHECK(keysymToUnicode(0xFF08) == 0 && keysymToUnicode(0xFFE1) == 0); // BackSpace, Shift type nothing
    skrumi::Interpreter interpreter;
    auto measured = interpreter.runSource("using graphics; int w = text_width(\"Skrumi\", 2);");
    CHECK(measured.success && interpreter.getGlobal("w").text() == "70");
    auto result = interpreter.runSource("using graphics; int c = rgb(18, 52, 86); close_window(); close_window();");
    CHECK(result.success);
    CHECK(interpreter.getGlobal("c").text() == "1193046");
    result = interpreter.runSource("gfx_rgb(256, 0, 0);");
    CHECK(!result.success);
    result = interpreter.runSource("gfx_rect(0, 0, 20, 20, 0);");
    CHECK(!result.success && result.errorMessage.find("open_window") != std::string::npos);
    result = interpreter.runSource("gfx_open(0, 100, \"invalid\");");
    CHECK(!result.success);
    CHECK(!interpreter.getContext().graphics);
    interpreter.reset();
    CHECK(!interpreter.getContext().graphics);
    std::cout << "GRAPHICS_RASTER_AND_VALIDATION_OK\n";
}
