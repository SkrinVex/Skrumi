#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
namespace skrumi::graphics {
class Surface;
class Font {
public:
    explicit Font(std::string bytes);
    explicit Font(std::vector<std::shared_ptr<Font>> fallback);
    bool supports(char32_t cp) const;
    ~Font();
    int width(const std::string& text, int pixels);
    void draw(Surface& surface, int x, int y, const std::string& text, int pixels, uint32_t color, int alpha = 255);
private:
    std::vector<std::shared_ptr<Font>> fallback_;
    Font& select(char32_t cp);
    struct State;
    std::unique_ptr<State> state_;
};
class Fonts {
public:
    int load(const std::string& bytes);
    Font& get(int handle);
    int withFallback(int body, int fallback);
    void release(int handle);
private:
    int add(std::shared_ptr<Font> font);
    std::vector<std::shared_ptr<Font>> faces_;
};
const char* fontLicense();
}
