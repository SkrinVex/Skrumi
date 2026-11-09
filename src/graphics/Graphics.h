#pragma once
#include "Image.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace skrumi::graphics {
class Font;

// Shared software renderer: identical pixels on Win32 and X11, no GPU/asset files.
class Surface {
public:
    Surface(int width, int height);
    void clear(uint32_t color);
    void resize(int width, int height);
    void rectangle(int x, int y, int width, int height, uint32_t color);
    // Mixes color into the rectangle: alpha 0 leaves it, 255 covers it.
    void blend(int x, int y, int width, int height, uint32_t color, int alpha);
    void circle(int x, int y, int radius, uint32_t color, int alpha = 255);
    void text(int x, int y, const std::string& text, int scale, uint32_t color, int alpha = 255);
    void line(int x1, int y1, int x2, int y2, uint32_t color, int alpha = 255);
    void frame(int x, int y, int width, int height, int thickness, uint32_t color);
    void ring(int x, int y, int radius, int thickness, uint32_t color, int alpha = 255);
    // The part (sx, sy, sw, sh) of a picture stretched over (x, y, width, height), its own
    // transparency mixed in and the whole scaled by opacity 0..255.
    void image(const Image& picture, int sx, int sy, int sw, int sh, int x, int y, int width, int height, int opacity);
    void imageEx(const Image& picture, int sx, int sy, int sw, int sh, int x, int y,
                 double scale, double radians, bool flipX, bool flipY, int opacity);
    void polygon(const std::vector<std::array<int, 2>>& points, uint32_t color);
    void ellipse(int x, int y, int rx, int ry, uint32_t color);
    void gradient(int x, int y, int width, int height, uint32_t from, uint32_t to, bool vertical);
    std::string png() const;
    // Width in pixels of the widest line of text drawn at this scale.
    static bool hasGlyph(char32_t cp);
    static int textWidth(const std::string& text, int scale);
    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<uint32_t>& pixels() const { return pixels_; }
    // Drawing is limited to the innermost clip rectangle, itself limited by the ones
    // around it; clear() still fills the whole surface.
    struct Clip { int left, top, right, bottom; };
    void pushClip(int x, int y, int width, int height);
    void popClip();
    void resetClip() { clips_.clear(); }
    Clip clip() const { return clips_.empty() ? Clip{0, 0, width_, height_} : clips_.back(); }
private:
    int width_, height_;
    std::vector<uint32_t> pixels_;
    std::vector<Clip> clips_;
};

// Offscreen RGB surfaces are local to the interpreter. Handle 0 selects its window.
class Canvases {
public:
    int create(int width, int height);
    Surface& get(int handle);
    void release(int handle);
    void select(int handle) { if (handle) get(handle); selected = handle; }
    int selected = 0;
private:
    int next_ = 1;
    std::map<int, std::unique_ptr<Surface>> surfaces_;
};

class Window;

// Unicode code point an X11 keysym types (Latin-1, Unicode keysyms, Cyrillic, keypad), or 0.
uint32_t keysymToUnicode(uint32_t keysym);

// One step of typing, in the order it happened within a frame: text or an editing key
// press (including OS repeats), with Ctrl as it was at that moment.
struct TextEdit {
    std::string text;
    int key = 0;
    bool control = false;
};

// Immediate-mode interface state: which element is under the mouse, which one has the
// keyboard, and text-field cursors. Elements register their rectangles every frame;
// the element under the mouse is resolved from the previous frame, top layer first,
// and a modal layer disables every layer below it.
class Ui {
public:
    void beginFrame(Window& window);
    void layerBegin(bool modal);
    void layerEnd();
    bool hover(const std::string& id, int x, int y, int width, int height, Window& window);
    bool click(const std::string& id, int x, int y, int width, int height, Window& window);
    // Edits and draws a single-line text field's content; returns the new text.
    std::string text(const std::string& id, int x, int y, int width, int height, const std::string& value,
                     int scale, uint32_t color, Window& window, Font* font = nullptr, int pixels = 14);
    bool focused(const std::string& id) const { return focus_ == id; }
    // A field focused before it is first drawn keeps the focus for one more frame.
    void setFocus(const std::string& id) {
        focus_ = id;
        focusFresh_ = true;
        if (!id.empty()) field(id).cursor = static_cast<size_t>(-1); // caret after the existing text
    }
    bool typing() const { return !focus_.empty(); }
    // True while the element is held by the mouse: from a press on it until the button is
    // released, even if the mouse leaves it. Other elements do not react meanwhile.
    bool drag(const std::string& id, int x, int y, int width, int height, Window& window);
    // Where inside the dragged element the press happened.
    int dragX() const { return dragX_; }
    int dragY() const { return dragY_; }
    // The wheel turn for this scroll area: the innermost area under the mouse that is not
    // covered by a higher layer gets it, the others get 0.
    int wheel(const std::string& id, int x, int y, int width, int height, Window& window);

private:
    struct Region {
        std::string id;
        int x, y, width, height, layer;
        bool focusable;
    };
    std::vector<Region> current_, previous_;
    std::vector<Region> scrolls_, previousScrolls_;
    std::string drag_, wheelOwner_;
    int dragX_ = 0, dragY_ = 0, hotLayer_ = -1;
    std::vector<int> stack_;      // open layers of this frame
    std::vector<bool> stackModal_;
    int nextLayer_ = 0, blockBelow_ = 0, modalLayer_ = 0;
    std::string hot_, focus_;
    bool clickTaken_ = false;
    bool focusFresh_ = false;
    struct Field { size_t cursor = 0, scroll = 0; bool selectedAll = false; };
    std::vector<std::pair<std::string, Field>> fields_;
    int layer() const { return stack_.empty() ? 0 : stack_.back(); }
    // Registers the element, cut to the clip rectangle, and returns where it can be hit.
    Region add(const std::string& id, int x, int y, int width, int height, bool focusable, Window& window);
    bool active(const Region& region, Window& window) const;
    Field& field(const std::string& id);
};

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    bool poll();
    void present();
    void setTargetFps(int fps);
    void setMouseVisible(bool visible);
    void setMouseCaptured(bool captured);
    bool mouseVisible() const { return mouseVisible_; }
    int mouseDeltaX() const { return mouseDeltaX_; }
    int mouseDeltaY() const { return mouseDeltaY_; }
    bool keyDown(const std::string& key) const;
    bool keyPressed(const std::string& key) const;
    // Pressed in this frame, or repeated by the OS while held: for editing and lists.
    bool keyRepeat(const std::string& key) const;
    // Text typed since the previous poll, as UTF-8, following layout, Shift and Caps Lock.
    const std::string& text() const { return text_; }
    const std::vector<TextEdit>& edits() const { return edits_; }
    int wheel() const { return wheel_; }
    bool doubleClicked() const { return doubleClick_; }
    std::string clipboard();
    void setClipboard(const std::string& text);
    int mouseX() const { return mouseX_; }
    int mouseY() const { return mouseY_; }
    bool focused() const { return focused_; }
    double delta() const { return delta_; }
    Surface& surface() { return surface_; }
    Ui& ui() { return ui_; }
    // Native backends feed one shared input state. Press edges are reset by poll().
    void keyEvent(int key, bool down);
    void textEvent(const std::string& utf8) {
        if (utf8.empty() || text_.size() >= 4096) return;
        text_ += utf8;
        if (edits_.size() < 4096) edits_.push_back({utf8, 0, down_[17]});
    }
    void wheelEvent(int steps) { wheel_ += steps; }
    void focusEvent(bool focused);
    void mouseEvent(int x, int y) {
        if (mouseKnown_) { mouseDeltaX_ += x - mouseX_; mouseDeltaY_ += y - mouseY_; }
        mouseKnown_ = true; mouseX_ = x; mouseY_ = y;
    }
    // Requests may be deferred by an application; destruction cannot be cancelled.
    void closeEvent() { if (interceptClose_) closeRequested_ = true; else open_ = false; }
    void destroyedEvent() { open_ = false; closeRequested_ = false; }
    void interceptClose(bool enabled) {
        interceptClose_ = enabled;
        if (!enabled && closeRequested_) { closeRequested_ = false; open_ = false; }
    }
    bool closeRequested() const { return closeRequested_; }
    void cancelClose() { closeRequested_ = false; }
    // The drawing area follows the window when the user may resize it.
    void setResizable(bool resizable);
    void setFullscreen(bool fullscreen);
    bool fullscreen() const;
    void setSize(int width, int height);
    std::array<double, 3> desktopMetrics() const;
    void sizeEvent(int width, int height) { pendingWidth_ = width; pendingHeight_ = height; }
    // True in the frame after the window changed size.
    bool resized() const { return resized_; }
private:
    struct Native;
    Surface surface_;
    Ui ui_;
    std::unique_ptr<Native> native_;
    std::array<bool, 256> down_{};
    std::array<bool, 256> pressed_{};
    std::array<bool, 256> repeated_{};
    std::string text_;
    std::vector<TextEdit> edits_;
    int wheel_ = 0;
    bool doubleClick_ = false;
    std::chrono::steady_clock::time_point lastClick_{};
    int lastClickX_ = -1000, lastClickY_ = -1000;
    bool open_ = true, focused_ = false, resized_ = false;
    bool interceptClose_ = false, closeRequested_ = false;
    int pendingWidth_ = 0, pendingHeight_ = 0;
    void applySize(int width, int height);
    int mouseX_ = 0, mouseY_ = 0;
    int mouseDeltaX_ = 0, mouseDeltaY_ = 0, targetFps_ = 0;
    bool mouseVisible_ = true, mouseCaptured_ = false, mouseKnown_ = false;
    void centerMouse();
    std::chrono::steady_clock::time_point nextFrame_{};
    double delta_ = 1.0 / 60.0;
    std::chrono::steady_clock::time_point lastPoll_ = std::chrono::steady_clock::now();
    static int keyCode(const std::string& key);
};

} // namespace skrumi::graphics
