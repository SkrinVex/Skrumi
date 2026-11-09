#include "Font.h"
#include "Graphics.h"
#include "FontLicenses.h"
#include "UnicodeLicenses.h"
#include "../audio/Mixer.h"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <ft2build.h>
#include FT_FREETYPE_H
namespace skrumi::graphics {
namespace {
void checked(FT_Error error) { if (error) throw std::runtime_error("Graphics Error: font operation failed (" + std::to_string(error) + ")"); }
void pixelSize(int pixels) { if (pixels < 8 || pixels > 96) throw std::runtime_error("Graphics Error: font size must be 8..96 pixels"); }
char32_t codepoint(const std::string& text, size_t& cursor) {
    unsigned char first = text[cursor++];
    if (first < 128) return first;
    int count = first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : first >= 0xc2 ? 1 : 0;
    if (!count || first > 0xf4 || text.size()-cursor < static_cast<size_t>(count)) return 0xfffd;
    char32_t value = first & ((1 << (6-count))-1);
    for (int i = 0; i < count; ++i) {
        unsigned char ch = text[cursor];
        if ((ch & 0xc0) != 0x80) return 0xfffd;
        ++cursor; value = (value << 6) | (ch & 63);
    }
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
        value < static_cast<char32_t>(count == 1 ? 128 : count == 2 ? 2048 : 65536)) return 0xfffd;
    return value;
}
}
struct Font::State {
    std::string bytes;
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    struct Glyph { Image image; int left, top, advance; };
    std::map<std::pair<int,char32_t>, Glyph> cache;
    ~State() { if (face) FT_Done_Face(face); if (library) FT_Done_FreeType(library); }
    const Glyph& glyph(char32_t character, int pixels) {
        auto key = std::make_pair(pixels, character);
        auto found = cache.find(key);
        if (found != cache.end()) return found->second;
        if (cache.size() >= 2048) cache.clear();
        checked(FT_Set_Pixel_Sizes(face, 0, pixels));
        checked(FT_Load_Char(face, character, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL));
        const auto& bitmap = face->glyph->bitmap;
        if (bitmap.width > 256 || bitmap.rows > 256 ||
            (bitmap.width && bitmap.rows && bitmap.pixel_mode != FT_PIXEL_MODE_GRAY))
            throw std::runtime_error("Graphics Error: unsupported or excessive glyph bitmap");
        Glyph glyph;
        glyph.left = face->glyph->bitmap_left; glyph.top = face->glyph->bitmap_top;
        glyph.advance = static_cast<int>((face->glyph->advance.x + 32) / 64);
        glyph.image.width = static_cast<int>(bitmap.width); glyph.image.height = static_cast<int>(bitmap.rows);
        glyph.image.pixels.resize(static_cast<size_t>(bitmap.width) * bitmap.rows);
        for (unsigned y = 0; y < bitmap.rows; ++y) {
            auto* row = bitmap.buffer + (bitmap.pitch >= 0 ? y : bitmap.rows-1-y) * static_cast<size_t>(std::abs(bitmap.pitch));
            for (unsigned x = 0; x < bitmap.width; ++x) glyph.image.pixels[y*bitmap.width+x] = uint32_t(row[x]) << 24;
        }
        return cache.emplace(key, std::move(glyph)).first->second;
    }
};
Font::Font(std::string bytes) : state_(std::make_unique<State>()) {
    if (bytes.empty() || bytes.size() > 8 * 1024 * 1024) throw std::runtime_error("Graphics Error: font data must be 1 byte..8 MiB");
    state_->bytes = std::move(bytes);
    checked(FT_Init_FreeType(&state_->library));
    checked(FT_New_Memory_Face(state_->library, reinterpret_cast<const FT_Byte*>(state_->bytes.data()),
                             static_cast<FT_Long>(state_->bytes.size()), 0, &state_->face));
    if (!FT_IS_SCALABLE(state_->face)) throw std::runtime_error("Graphics Error: scalable outline font required");
    checked(FT_Select_Charmap(state_->face, FT_ENCODING_UNICODE));
}
Font::Font(std::vector<std::shared_ptr<Font>> fallback) {
    for (auto& face : fallback) {
        auto append = [&](const std::shared_ptr<Font>& font) {
            if (std::find(fallback_.begin(), fallback_.end(), font) == fallback_.end()) fallback_.push_back(font);
        };
        if (face->state_) append(face);
        else for (auto& font : face->fallback_) append(font);
    }
    if (fallback_.empty() || fallback_.size() > 32) throw std::runtime_error("Graphics Error: fallback chain must contain 1..32 faces");
}
Font::~Font() = default;
bool Font::supports(char32_t cp) const {
    if (cp == U'\n') return true;
    if (state_) return FT_Get_Char_Index(state_->face, cp) != 0;
    for (auto& font : fallback_) if (font->supports(cp)) return true;
    return false;
}
Font& Font::select(char32_t cp) {
    if (state_) return *this;
    for (auto& font : fallback_) if (font->supports(cp)) return font->select(cp);
    return fallback_.front()->select(cp);
}
int Font::width(const std::string& text, int pixels) {
    pixelSize(pixels);
    int current = 0, widest = 0;
    for (size_t cursor = 0; cursor < text.size();) {
        auto ch = codepoint(text, cursor);
        if (ch == '\n') { widest = std::max(widest, current); current = 0; continue; }
        current += select(ch).state_->glyph(ch, pixels).advance;
        if (current > 10000000) throw std::runtime_error("Graphics Error: text extent is too large");
    }
    return std::max(current, widest);
}
void Font::draw(Surface& surface, int x, int y, const std::string& text, int pixels, uint32_t color, int opacity) {
    if (opacity < 0 || opacity > 255) throw std::runtime_error("Graphics Error: alpha must be 0..255");
    pixelSize(pixels);
    auto& primary = select(U' ').state_;
    checked(FT_Set_Pixel_Sizes(primary->face, 0, pixels));
    int ascender = static_cast<int>((primary->face->size->metrics.ascender + 63) / 64);
    int64_t left = x, top = y;
    for (size_t cursor = 0; cursor < text.size();) {
        auto ch = codepoint(text, cursor);
        if (ch == '\n') { left = x; top += pixels + 4; continue; }
        const auto& glyph = select(ch).state_->glyph(ch, pixels);
        const auto area = surface.clip();
        auto gx = left + glyph.left, gy = top + ascender - glyph.top;
        if (gx < area.right && gy < area.bottom && gx + glyph.image.width > area.left && gy + glyph.image.height > area.top) {
            // Glyph cache stores coverage only; drawing does not retain an application color.
            for (int row = 0; row < glyph.image.height; ++row) for (int col = 0; col < glyph.image.width; ++col) {
                int alpha = static_cast<int>(glyph.image.pixels[static_cast<size_t>(row) * glyph.image.width + col] >> 24);
                if (alpha && gx+col >= area.left && gx+col < area.right && gy+row >= area.top && gy+row < area.bottom)
                    surface.blend(static_cast<int>(gx+col), static_cast<int>(gy+row), 1, 1, color, (alpha * opacity + 127) / 255);
            }
        }
        left += glyph.advance;
    }
}
int Fonts::load(const std::string& bytes) { return add(std::make_shared<Font>(bytes)); }
int Fonts::withFallback(int body, int fallback) {
    get(body); get(fallback);
    std::vector<std::shared_ptr<Font>> chain;
    // A composite is immutable; new handles only refer to existing faces, so cycles
    // are impossible. Keep a bounded depth through the global 32-handle limit.
    chain.push_back(faces_[body - 1]); chain.push_back(faces_[fallback - 1]);
    return add(std::make_shared<Font>(std::move(chain)));
}
int Fonts::add(std::shared_ptr<Font> face) {
    for (size_t i = 0; i < faces_.size(); ++i) if (!faces_[i]) { faces_[i] = std::move(face); return static_cast<int>(i+1); }
    if (faces_.size() >= 32) throw std::runtime_error("Graphics Error: font handle limit");
    faces_.push_back(std::move(face)); return static_cast<int>(faces_.size());
}
Font& Fonts::get(int handle) {
    if (handle < 1 || static_cast<size_t>(handle) > faces_.size() || !faces_[handle-1]) throw std::runtime_error("Graphics Error: invalid font handle");
    return *faces_[handle-1];
}
void Fonts::release(int handle) { get(handle); faces_[handle-1].reset(); }
const char* fontLicense() { static const std::string licenses = std::string(skrumiFontLicense()) + "\n" + skrumiUnicodeLicense() + "\n" + audio::licenses(); return licenses.c_str(); }
}
