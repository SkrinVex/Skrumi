#include "Graphics.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace skrumi::graphics {
namespace {
[[noreturn]] void fail(const char* message) { throw std::runtime_error(std::string("Graphics Error: ") + message); }
void be32(std::string& out, uint32_t n) { for (int s = 24; s >= 0; s -= 8) out += static_cast<char>(n >> s); }
void chunk(std::string& out, const char* type, const std::string& data) {
    be32(out, static_cast<uint32_t>(data.size()));
    size_t start = out.size(); out.append(type, 4); out += data;
    uint32_t crc = 0xffffffffU;
    for (size_t i = start; i < out.size(); ++i) {
        crc ^= static_cast<unsigned char>(out[i]);
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    }
    be32(out, crc ^ 0xffffffffU);
}
}

void Surface::polygon(const std::vector<std::array<int, 2>>& points, uint32_t color) {
    if (points.size() < 3 || points.size() > 4096) fail("polygon needs 3..4096 points");
    auto area = clip();
    std::vector<double> intersections; intersections.reserve(points.size());
    // Pixel centres and half-open edges avoid double coverage of shared vertices.
    for (int y = area.top; y < area.bottom; ++y) {
        intersections.clear(); double row = y + 0.5;
        for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
            double ax = points[j][0], ay = points[j][1], bx = points[i][0], by = points[i][1];
            if ((ay <= row && row < by) || (by <= row && row < ay))
                intersections.push_back(ax + (row - ay) * (bx - ax) / (by - ay));
        }
        std::sort(intersections.begin(), intersections.end());
        for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
            int left = static_cast<int>(std::clamp(std::ceil(intersections[i] - 0.5), double(area.left), double(area.right)));
            int right = static_cast<int>(std::clamp(std::ceil(intersections[i+1] - 0.5), double(area.left), double(area.right)));
            if (right > left) rectangle(left, y, right - left, 1, color);
        }
    }
}

void Surface::ellipse(int x, int y, int rx, int ry, uint32_t color) {
    if (rx < 0 || ry < 0 || rx > 8192 || ry > 8192) fail("ellipse radii must be 0..8192");
    auto area = clip();
    int64_t top = std::max<int64_t>(area.top, int64_t(y) - ry), bottom = std::min<int64_t>(area.bottom - 1, int64_t(y) + ry);
    for (int64_t row = top; row <= bottom; ++row) {
        double dy = ry ? double(row - y) / ry : 0;
        int64_t dx = static_cast<int64_t>(rx * std::sqrt(std::max(0.0, 1 - dy * dy)));
        int64_t left = std::max<int64_t>(area.left, int64_t(x) - dx), right = std::min<int64_t>(area.right - 1, int64_t(x) + dx);
        if (left <= right) rectangle(static_cast<int>(left), static_cast<int>(row), static_cast<int>(right-left+1), 1, color);
    }
}

void Surface::gradient(int x, int y, int width, int height, uint32_t from, uint32_t to, bool vertical) {
    if (width < 0 || height < 0) fail("gradient size must not be negative");
    auto area = clip();
    int64_t start = vertical ? y : x, length = vertical ? height : width;
    int64_t first = std::max<int64_t>(start, vertical ? area.top : area.left);
    int64_t last = std::min<int64_t>(start + length, vertical ? area.bottom : area.right);
    for (int64_t at = first; at < last; ++at) {
        double t = length <= 1 ? 0 : double(at - start) / double(length - 1);
        uint32_t color = 0;
        for (int shift : {0, 8, 16}) color |= static_cast<uint32_t>(std::lround(((from >> shift) & 255) * (1-t) + ((to >> shift) & 255) * t)) << shift;
        if (vertical) rectangle(x, static_cast<int>(at), width, 1, color);
        else rectangle(static_cast<int>(at), y, 1, height, color);
    }
}

void Surface::imageEx(const Image& picture, int sx, int sy, int sw, int sh, int x, int y,
                      double scale, double radians, bool flipX, bool flipY, int opacity) {
    if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0 || int64_t(sx)+sw > picture.width || int64_t(sy)+sh > picture.height)
        fail("source rectangle is outside the image");
    if (!std::isfinite(scale) || scale <= 0 || scale > 1024 || !std::isfinite(radians)) fail("scale must be > 0..1024 and angle finite");
    if (opacity < 0 || opacity > 255) fail("opacity must be 0..255");
    if (picture.pixels.size() != size_t(picture.width)*picture.height) fail("invalid image pixels");
    double w = sw * scale, h = sh * scale, cx = x + w/2, cy = y + h/2;
    double c = std::cos(radians), s = std::sin(radians);
    double ex = (std::abs(c)*w + std::abs(s)*h)/2, ey = (std::abs(s)*w + std::abs(c)*h)/2;
    auto area = clip();
    int left = static_cast<int>(std::clamp(std::floor(cx-ex), double(area.left), double(area.right)));
    int right = static_cast<int>(std::clamp(std::ceil(cx+ex), double(area.left), double(area.right)));
    int top = static_cast<int>(std::clamp(std::floor(cy-ey), double(area.top), double(area.bottom)));
    int bottom = static_cast<int>(std::clamp(std::ceil(cy+ey), double(area.top), double(area.bottom)));
    for (int row = top; row < bottom; ++row) for (int col = left; col < right; ++col) {
        double dx = col + 0.5 - cx, dy = row + 0.5 - cy;
        double u = (c*dx + s*dy)/scale + sw/2.0, v = (-s*dx + c*dy)/scale + sh/2.0;
        if (u < 0 || v < 0 || u >= sw || v >= sh) continue;
        int ix = static_cast<int>(u), iy = static_cast<int>(v);
        if (flipX) ix = sw - 1 - ix;
        if (flipY) iy = sh - 1 - iy;
        uint32_t pixel = picture.pixels[size_t(sy+iy)*picture.width + sx+ix];
        blend(col, row, 1, 1, pixel & 0xffffff, static_cast<int>(((pixel >> 24)*opacity+127)/255));
    }
}

std::string Surface::png() const {
    // PNG with stored DEFLATE blocks: lossless, deterministic and dependency-free.
    std::string raw; raw.reserve((size_t(width_)*3+1)*height_);
    uint32_t a = 1, b = 0;
    auto byte = [&](unsigned char value) { raw += static_cast<char>(value); a = (a+value)%65521; b = (b+a)%65521; };
    for (int y=0; y<height_; ++y) {
        byte(0);
        for (int x=0; x<width_; ++x) {
            auto p = pixels_[size_t(y)*width_+x]; byte(p>>16); byte(p>>8); byte(p);
        }
    }
    std::string deflate("\x78\x01", 2);
    for (size_t at=0; at<raw.size();) {
        uint16_t n = static_cast<uint16_t>(std::min<size_t>(65535, raw.size()-at));
        deflate += at+n == raw.size() ? '\1' : '\0';
        deflate += static_cast<char>(n); deflate += static_cast<char>(n>>8);
        uint16_t inverse = static_cast<uint16_t>(~n);
        deflate += static_cast<char>(inverse); deflate += static_cast<char>(inverse>>8);
        deflate.append(raw, at, n); at += n;
    }
    be32(deflate, (b<<16)|a);
    std::string result("\x89PNG\r\n\x1a\n", 8), header;
    be32(header, width_); be32(header, height_); header.append("\x08\x02\0\0\0", 5);
    chunk(result, "IHDR", header); chunk(result, "IDAT", deflate); chunk(result, "IEND", "");
    return result;
}

int Canvases::create(int width, int height) {
    size_t used = 0; for (const auto& entry : surfaces_) used += entry.second->pixels().size();
    if (width <= 0 || height <= 0 || int64_t(width)*height > 33554432 - int64_t(used)) fail("canvas memory limit: 32 million pixels per interpreter");
    if (surfaces_.size() >= 32 || next_ == std::numeric_limits<int>::max()) fail("canvas handle limit reached");
    auto canvas = std::make_unique<Surface>(width, height);
    int handle = next_++; surfaces_.emplace(handle, std::move(canvas)); return handle;
}
Surface& Canvases::get(int handle) { auto found = surfaces_.find(handle); if (found == surfaces_.end()) fail("invalid canvas handle"); return *found->second; }
void Canvases::release(int handle) { get(handle); if (selected == handle) selected = 0; surfaces_.erase(handle); }
}
