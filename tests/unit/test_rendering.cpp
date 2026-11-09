#include "../../src/graphics/Graphics.h"
#include <iostream>
#include <climits>
#include <cmath>
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL " #c << ':' << __LINE__ << '\n'; return 1; } } while (0)
int main() {
    using namespace skrumi::graphics;
    Surface s(10,10);
    s.polygon({{2,2},{8,2},{8,8},{2,8}},0xff0000);
    CHECK(s.pixels()[22] == 0xff0000 && s.pixels()[77] == 0xff0000 && s.pixels()[88] == 0);
    auto pixels = s.pixels();
    s.polygon({{INT_MIN,INT_MIN},{INT_MIN+10,INT_MIN},{INT_MIN,INT_MIN+10}},0xffffff);
    CHECK(s.pixels() == pixels);
    s.clear(0); s.ellipse(5,5,3,2,0x00ff00);
    CHECK(s.pixels()[55] == 0x00ff00 && s.pixels()[58] == 0x00ff00 && s.pixels()[59] == 0);
    s.gradient(0,0,10,10,0,0xffffff,true);
    CHECK(s.pixels()[0] == 0 && s.pixels()[90] == 0xffffff);
    Image source{2,2,{0xffff0000,0xff00ff00,0xff0000ff,0xffffffff}};
    s.clear(0); s.imageEx(source,0,0,2,2,0,0,1,0,false,false,255);
    CHECK(s.pixels()[0] == 0xff0000 && s.pixels()[1] == 0x00ff00 && s.pixels()[10] == 0x0000ff);
    s.imageEx(source,0,0,2,2,0,0,1,0,true,false,255);
    CHECK(s.pixels()[0] == 0x00ff00 && s.pixels()[1] == 0xff0000);
    s.imageEx(source,0,0,2,2,0,0,1,std::acos(-1.0)/2,false,false,255);
    CHECK(s.pixels()[0] == 0x0000ff && s.pixels()[1] == 0xff0000 && s.pixels()[10] == 0xffffff);
    pixels = s.pixels();
    s.imageEx(source,0,0,2,2,INT_MAX,INT_MIN,1,1,false,false,255);
    CHECK(s.pixels() == pixels);
    Image png = decodeImage(s.png());
    CHECK(png.width == 10 && png.height == 10);
    for(size_t i=0;i<png.pixels.size();++i) CHECK(png.pixels[i] == (s.pixels()[i]|0xff000000U));
    // More than one DEFLATE stored block, plus non-uniform RGB bytes.
    Surface large(257,100); large.gradient(0,0,257,100,0x12ab34,0xfe23dc,false);
    png = decodeImage(large.png());
    for(size_t i=0;i<png.pixels.size();++i) CHECK(png.pixels[i] == (large.pixels()[i]|0xff000000U));
    Canvases a,b; int handle = a.create(10,10); a.select(handle); a.release(handle);
    CHECK(a.selected == 0);
    bool rejected=false; try { b.get(handle); } catch (...) { rejected=true; }
    CHECK(rejected);
    std::cout << "RENDERING_OK\n";
}
