#include "Graphics.h"
#include "skrumi/Platform.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <mutex>
#elif defined(__ANDROID__)
#include "skrumi/Android.h"
#elif defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
// The page's side of a playground window (see Window::Native below).
EM_JS(int, skrumi_canvas_open, (int width, int height, const char* title), {
    if (!Module.canvasOpen) return 0;
    Module.canvasOpen(width, height, UTF8ToString(title));
    return 1;
});
EM_JS(void, skrumi_canvas_present, (const void* pixels, int width, int height), {
    Module.canvasPresent(HEAPU8.subarray(pixels, pixels + width * height * 4), width, height);
});
EM_JS(void, skrumi_canvas_poll, (), {
    return Asyncify.handleAsync(() => Module.canvasPoll());
});
EM_JS(void, skrumi_canvas_close, (), {
    if (Module.canvasClose) Module.canvasClose();
});
EM_JS(void, skrumi_canvas_resizable, (int resizable), {
    if (Module.canvasResizable) Module.canvasResizable(!!resizable);
});
#else
#include <sys/ipc.h>
#include <sys/shm.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#endif

namespace skrumi::graphics {
namespace {
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("Graphics Error: " + message); }

std::string utf8(uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}
}

// Unicode for the keysyms a keyboard types: Latin-1, the Unicode block and Cyrillic.
uint32_t keysymToUnicode(uint32_t sym) {
    if ((sym >= 0x20 && sym <= 0x7E) || (sym >= 0xA0 && sym <= 0xFF)) return sym;
    if (sym >= 0x01000000 && sym <= 0x0110FFFF) return sym - 0x01000000;
    static const uint16_t cyrillic[32] = {
        0x44E, 0x430, 0x431, 0x446, 0x434, 0x435, 0x444, 0x433, 0x445, 0x438, 0x439, 0x43A, 0x43B, 0x43C, 0x43D, 0x43E,
        0x43F, 0x44F, 0x440, 0x441, 0x442, 0x443, 0x436, 0x432, 0x44C, 0x44B, 0x437, 0x448, 0x44D, 0x449, 0x447, 0x44A};
    if (sym >= 0x6C0 && sym <= 0x6DF) return cyrillic[sym - 0x6C0];
    if (sym >= 0x6E0 && sym <= 0x6FF) return cyrillic[sym - 0x6E0] - 0x20u;
    switch (sym) {
        case 0x6A3: return 0x451; case 0x6B3: return 0x401; // ё Ё
        case 0x6A4: return 0x454; case 0x6B4: return 0x404; // є Є
        case 0x6A6: return 0x456; case 0x6B6: return 0x406; // і І
        case 0x6A7: return 0x457; case 0x6B7: return 0x407; // ї Ї
        case 0x6AD: return 0x491; case 0x6BD: return 0x490; // ґ Ґ
        case 0x6AE: return 0x45E; case 0x6BE: return 0x40E; // ў Ў
        case 0xFF80: return ' ';
        case 0xFFAA: return '*'; case 0xFFAB: return '+'; case 0xFFAD: return '-';
        case 0xFFAE: return '.'; case 0xFFAF: return '/';
        default: break;
    }
    if (sym >= 0xFFB0 && sym <= 0xFFB9) return '0' + (sym - 0xFFB0);
    return 0;
}

#ifdef _WIN32
struct Window::Native {
    Window& owner;
    HWND hwnd = nullptr;
    BITMAPINFO bitmap{};
    wchar_t highSurrogate = 0;
    int wheelRemainder = 0;
    bool full = false, resizable = true;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    LONG_PTR windowStyle = WS_OVERLAPPEDWINDOW;
    bool fullscreen() const { return full; }
    void setFullscreen(bool on) {
        if (full == on) return;
        if (on) {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (!GetWindowPlacement(hwnd, &placement) ||
                !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
                fail("cannot read monitor geometry");
            windowStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
            full = true;
            SetWindowLongPtrW(hwnd, GWL_STYLE, windowStyle & ~LONG_PTR(WS_OVERLAPPEDWINDOW));
            SetWindowPos(hwnd, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                         monitor.rcMonitor.right - monitor.rcMonitor.left,
                         monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        } else {
            full = false;
            LONG_PTR style = resizable ? windowStyle | WS_THICKFRAME | WS_MAXIMIZEBOX
                                      : windowStyle & ~LONG_PTR(WS_THICKFRAME | WS_MAXIMIZEBOX);
            SetWindowLongPtrW(hwnd, GWL_STYLE, style);
            SetWindowPlacement(hwnd, &placement);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    explicit Native(Window& window) : owner(window) {}
    ~Native() { if (hwnd) DestroyWindow(hwnd); }
    void paint(HDC dc) {
        auto& surface = owner.surface();
        StretchDIBits(dc, 0, 0, surface.width(), surface.height(), 0, 0, surface.width(), surface.height(),
                     surface.pixels().data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
    }
    static LRESULT CALLBACK procedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Native*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Native*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(handle, message, wparam, lparam);
        switch (message) {
            case WM_SETCURSOR:
                if (LOWORD(lparam) == HTCLIENT) { SetCursor(self->owner.mouseVisible() ? LoadCursorW(nullptr, IDC_ARROW) : nullptr); return TRUE; }
                break;
            case WM_CLOSE: self->owner.closeEvent(); return 0;
            case WM_DESTROY: self->owner.destroyedEvent(); return 0;
            case WM_SETFOCUS: self->owner.focusEvent(true); return 0;
            case WM_KILLFOCUS: self->owner.focusEvent(false); return 0;
            case WM_KEYDOWN: case WM_SYSKEYDOWN: self->owner.keyEvent(static_cast<int>(wparam), true); break;
            case WM_KEYUP: case WM_SYSKEYUP: self->owner.keyEvent(static_cast<int>(wparam), false); break;
            case WM_MOUSEMOVE: self->owner.mouseEvent(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)); return 0;
            case WM_CHAR: {
                auto unit = static_cast<wchar_t>(wparam);
                if (unit >= 0xD800 && unit <= 0xDBFF) { self->highSurrogate = unit; return 0; }
                uint32_t cp = unit;
                if (unit >= 0xDC00 && unit <= 0xDFFF) {
                    if (!self->highSurrogate) return 0;
                    cp = 0x10000 + ((uint32_t(self->highSurrogate) - 0xD800) << 10) + (uint32_t(unit) - 0xDC00);
                }
                self->highSurrogate = 0;
                if (cp >= 0x20 && cp != 0x7F) self->owner.textEvent(utf8(cp));
                return 0;
            }
            case WM_MOUSEWHEEL: {
                // The wheel acts where the cursor is, which the message carries in screen
                // coordinates; no mouse move may have been reported since the cursor got there.
                POINT at{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (ScreenToClient(handle, &at)) self->owner.mouseEvent(at.x, at.y);
                self->wheelRemainder += GET_WHEEL_DELTA_WPARAM(wparam);
                int steps = self->wheelRemainder / WHEEL_DELTA;
                self->wheelRemainder -= steps * WHEEL_DELTA;
                if (steps) self->owner.wheelEvent(steps);
                return 0;
            }
            case WM_MBUTTONDOWN:
                self->owner.mouseEvent(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                self->owner.keyEvent(4, true); return 0;
            case WM_MBUTTONUP:
                self->owner.mouseEvent(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                self->owner.keyEvent(4, false); return 0;
            case WM_LBUTTONDOWN:
                self->owner.mouseEvent(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                SetCapture(handle); self->owner.keyEvent(1, true); return 0;
            case WM_LBUTTONUP:
                self->owner.mouseEvent(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                self->owner.keyEvent(1, false);
                if (!self->owner.keyDown("MOUSE_RIGHT")) ReleaseCapture();
                return 0;
            case WM_RBUTTONDOWN:
                self->owner.mouseEvent(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                SetCapture(handle); self->owner.keyEvent(2, true); return 0;
            case WM_RBUTTONUP:
                self->owner.mouseEvent(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                self->owner.keyEvent(2, false);
                if (!self->owner.keyDown("MOUSE_LEFT")) ReleaseCapture();
                return 0;
            case WM_CAPTURECHANGED:
                self->owner.keyEvent(1, false); self->owner.keyEvent(2, false); return 0;
            case WM_GETMINMAXINFO: {
                if (!self->full) {
                    auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
                    RECT small{0, 0, 64, 64}, large{0, 0, 4096, 4096};
                    auto style = static_cast<DWORD>(GetWindowLongPtrW(handle, GWL_STYLE));
                    AdjustWindowRect(&small, style, FALSE);
                    AdjustWindowRect(&large, style, FALSE);
                    limits->ptMinTrackSize = {small.right - small.left, small.bottom - small.top};
                    limits->ptMaxTrackSize = {large.right - large.left, large.bottom - large.top};
                }
                return 0;
            }
            case WM_SIZE:
                if (wparam != SIZE_MINIMIZED && LOWORD(lparam) > 0 && HIWORD(lparam) > 0)
                    self->owner.sizeEvent(LOWORD(lparam), HIWORD(lparam));
                return 0;
            case WM_ERASEBKGND: return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC dc = BeginPaint(handle, &ps);
                self->paint(dc);
                EndPaint(handle, &ps);
                return 0;
            }
        }
        return DefWindowProcW(handle, message, wparam, lparam);
    }
    void open(const std::string& title) {
        static std::once_flag registration;
        std::call_once(registration, [] {
            WNDCLASSW cls{};
            cls.lpfnWndProc = procedure;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpszClassName = L"SkrumiGraphicsWindow";
            cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
            if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) fail("cannot register Win32 window class");
        });
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title.data(), static_cast<int>(title.size()), nullptr, 0);
        if (length <= 0) fail("window title must be valid UTF-8");
        std::wstring wide(size_t(length), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title.data(), static_cast<int>(title.size()), wide.data(), length);
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = owner.surface().width();
        bitmap.bmiHeader.biHeight = -owner.surface().height();
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;
        DWORD style = WS_OVERLAPPEDWINDOW;
        RECT rect{0, 0, owner.surface().width(), owner.surface().height()};
        AdjustWindowRect(&rect, style, FALSE);
        hwnd = CreateWindowExW(0, L"SkrumiGraphicsWindow", wide.c_str(), style,
                               CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                               nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd) fail("cannot create Win32 window");
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    void resizeBuffers() {
        bitmap.bmiHeader.biWidth = owner.surface().width();
        bitmap.bmiHeader.biHeight = -owner.surface().height();
    }
    void setResizable(bool on) {
        resizable = on;
        if (full) return;
        int width = owner.surface().width(), height = owner.surface().height();
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style = resizable ? (style | WS_THICKFRAME | WS_MAXIMIZEBOX) : (style & ~LONG_PTR(WS_THICKFRAME | WS_MAXIMIZEBOX));
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        RECT rect{0, 0, width, height};
        AdjustWindowRect(&rect, static_cast<DWORD>(style), FALSE);
        SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void setSize(int width, int height) {
        RECT rect{0, 0, width, height};
        AdjustWindowRect(&rect, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE);
        SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void poll() {
        MSG message;
        while (PeekMessageW(&message, hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    void present() {
        HDC dc = GetDC(hwnd);
        if (!dc) fail("cannot acquire Win32 device context");
        paint(dc);
        ReleaseDC(hwnd, dc);
    }
    std::string clipboard() {
        std::string out;
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(hwnd)) return out;
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
            if (auto* text = static_cast<const wchar_t*>(GlobalLock(data))) {
                int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
                if (size > 1) {
                    out.resize(static_cast<size_t>(size - 1));
                    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
                }
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
        return out;
    }
    void setClipboard(const std::string& text) {
        int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (length <= 0) return;
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size_t(length) * sizeof(wchar_t));
        if (!memory) return;
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, static_cast<wchar_t*>(GlobalLock(memory)), length);
        GlobalUnlock(memory);
        if (!OpenClipboard(hwnd)) { GlobalFree(memory); return; }
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        CloseClipboard();
    }
};
#elif defined(__ANDROID__)
struct Window::Native {
    Window& owner;
    bool resize = true, full = false;
    explicit Native(Window& window) : owner(window) {}
    ~Native() { android::host().close(); }
    void open(const std::string& title) {
        android::host().open(owner.surface().width(), owner.surface().height(), title);
        owner.focusEvent(true);
    }
    void poll() {
        for (const auto& e : android::host().poll()) {
            switch(e.kind) {
                case android::Event::Key: owner.keyEvent(e.a, e.b != 0); break;
                case android::Event::Text: owner.textEvent(e.text); break;
                case android::Event::Mouse: owner.mouseEvent(e.a, e.b); break;
                case android::Event::Wheel: owner.wheelEvent(e.a); break;
                case android::Event::Focus: owner.focusEvent(e.a != 0); break;
                case android::Event::Size: if (resize) owner.sizeEvent(e.a, e.b); break;
                case android::Event::Close: owner.closeEvent(); break;
            }
        }
    }
    void present() {
        const auto& s = owner.surface();
        android::host().present(s.pixels().data(), s.width(), s.height());
    }
    void resizeBuffers() {}
    void setSize(int w, int h) { owner.sizeEvent(w, h); }
    void setResizable(bool enabled) { resize = enabled; android::host().resizable(enabled); }
    void setFullscreen(bool enabled) { android::host().fullscreen(enabled); full = enabled; }
    bool fullscreen() const { return full; }
    std::string clipboard() { return android::host().clipboard(); }
    void setClipboard(const std::string& text) { android::host().clipboard(text); }
};
#elif defined(__EMSCRIPTEN__)
// The browser playground: the page hands the worker a canvas. A window draws into it,
// and each poll lets the browser deliver the keys and the mouse, which needs the
// build with Asyncify; the ordinary one has no canvas and reports that.
namespace {
Window* shown = nullptr; // the window the page's input goes to
} // namespace

struct Window::Native {
    Window& owner;
    std::vector<uint32_t> rgba; // the frame as the canvas takes it: R, G, B, A bytes
    explicit Native(Window& window) : owner(window) {}
    ~Native() {
        if (shown == &owner) {
            shown = nullptr;
            skrumi_canvas_close();
        }
    }
    void open(const std::string& title) {
        if (!skrumi_canvas_open(owner.surface().width(), owner.surface().height(), title.c_str()))
            fail("windows are not available here; run the program with skrumi on a computer");
        shown = &owner;
        owner.focusEvent(true);
        present();
    }
    void poll() { skrumi_canvas_poll(); }
    void present() {
        const auto& surface = owner.surface();
        const auto& pixels = surface.pixels();
        rgba.resize(pixels.size());
        for (size_t i = 0; i < pixels.size(); ++i) {
            uint32_t c = pixels[i];
            rgba[i] = 0xff000000u | ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff);
        }
        skrumi_canvas_present(rgba.data(), surface.width(), surface.height());
    }
    bool fullscreen() const { return false; }
    void setFullscreen(bool on) { if (on) fail("fullscreen requires a native Windows or X11 window"); }
    void resizeBuffers() {}
    void setResizable(bool resizable) { skrumi_canvas_resizable(resizable); }
    void setSize(int width, int height) { owner.sizeEvent(width, height); }
    std::string clipboard() { return ""; }
    void setClipboard(const std::string&) {}
};

// What the page reports from the canvas, in the codes the native backends use.
extern "C" {
EMSCRIPTEN_KEEPALIVE void skrumi_key(int key, int down) { if (shown) shown->keyEvent(key, down != 0); }
EMSCRIPTEN_KEEPALIVE void skrumi_char(int codepoint) {
    if (shown && codepoint >= 0x20 && codepoint != 0x7f && codepoint <= 0x10ffff) shown->textEvent(utf8(static_cast<uint32_t>(codepoint)));
}
EMSCRIPTEN_KEEPALIVE void skrumi_mouse(int x, int y) { if (shown) shown->mouseEvent(x, y); }
EMSCRIPTEN_KEEPALIVE void skrumi_wheel(int steps) { if (shown) shown->wheelEvent(steps); }
EMSCRIPTEN_KEEPALIVE void skrumi_focus(int focused) { if (shown) shown->focusEvent(focused != 0); }
EMSCRIPTEN_KEEPALIVE void skrumi_size(int width, int height) { if (shown) shown->sizeEvent(width, height); }
EMSCRIPTEN_KEEPALIVE void skrumi_close() { if (shown) shown->closeEvent(); }
}
#else
struct Window::Native {
    Window& owner;
    xcb_connection_t* connection = nullptr;
    xcb_window_t window = XCB_NONE, root = XCB_NONE;
    xcb_atom_t stateAtom = XCB_NONE, fullscreenAtom = XCB_NONE;
    bool full = false, requestedFull = false;
    int windowedWidth = 0, windowedHeight = 0;
    bool fullscreen() const { return full; }
    bool hasAtom(xcb_window_t target, xcb_atom_t property, xcb_atom_t value) {
        auto cookie = xcb_get_property(connection, 0, target, property, XCB_ATOM_ATOM, 0, 4096);
        Reply<xcb_get_property_reply_t> reply(xcb_get_property_reply(connection, cookie, nullptr), &std::free);
        if (!reply || reply->type != XCB_ATOM_ATOM || reply->format != 32) return false;
        auto* values = static_cast<xcb_atom_t*>(xcb_get_property_value(reply.get()));
        auto count = xcb_get_property_value_length(reply.get()) / 4;
        return std::find(values, values + count, value) != values + count;
    }
    void setFullscreen(bool on) {
        if (requestedFull == on && full == on) return;
        if (!hasAtom(root, atom("_NET_SUPPORTED"), fullscreenAtom))
            fail("fullscreen requires an EWMH-compatible X11 window manager");
        // Let the window manager choose the monitor, remove decorations and restore
        // the original geometry. A fixed-size window must not constrain fullscreen.
        if (on) {
            if (!full && !requestedFull) {
                windowedWidth = owner.surface().width(); windowedHeight = owner.surface().height();
            }
            sizeHints(64, 64, true);
        } else sizeHints(windowedWidth, windowedHeight);
        xcb_client_message_event_t event{};
        event.response_type = XCB_CLIENT_MESSAGE;
        event.format = 32;
        event.window = window;
        event.type = stateAtom;
        event.data.data32[0] = on ? 1 : 0;
        event.data.data32[1] = fullscreenAtom;
        event.data.data32[3] = 1; // normal application, EWMH source indication
        check(xcb_send_event_checked(connection, 0, root,
              XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
              reinterpret_cast<const char*>(&event)));
        xcb_flush(connection);
        requestedFull = on;
    }
    xcb_pixmap_t back = XCB_NONE;
    xcb_gcontext_t gc = XCB_NONE;
    xcb_atom_t protocols = XCB_NONE, deleteWindow = XCB_NONE;
    uint8_t depth = 0, bits = 0, byteOrder = 0;
    uint32_t red = 0, green = 0, blue = 0;
    size_t stride = 0, rowsPerRequest = 0;
    std::vector<uint8_t> image;
    std::array<int, 256> keymap{};
    std::vector<uint32_t> keysyms;    // every keysym of every keycode, for typed text
    unsigned keysymsPerCode = 0, minKeycode = 0;
    xcb_atom_t clipboardAtom = XCB_NONE, utf8Atom = XCB_NONE, targetsAtom = XCB_NONE, transferAtom = XCB_NONE;
    std::string ownedClipboard;       // what this window offers while it owns CLIPBOARD
    // MIT-SHM hands the frame over in shared memory instead of through the socket. A
    // remote display, or a system without System V shared memory, gets xcb_put_image.
    uint32_t shmSegment = 0;
    uint8_t* shmPixels = nullptr;
    bool shmPending = false;          // the server may still be reading the last frame
    explicit Native(Window& w) : owner(w) {}
    ~Native() {
        if (connection) {
            detachShm();
            if (gc) xcb_free_gc(connection, gc);
            if (back) xcb_free_pixmap(connection, back);
            if (window) xcb_destroy_window(connection, window);
            xcb_flush(connection);
            xcb_disconnect(connection);
        }
    }
    template<class T> using Reply = std::unique_ptr<T, decltype(&std::free)>;
    void check(xcb_void_cookie_t cookie) {
        Reply<xcb_generic_error_t> error(xcb_request_check(connection, cookie), &std::free);
        if (error) fail("X11 request failed (code " + std::to_string(error->error_code) + ")");
        if (xcb_connection_has_error(connection)) fail("X11 connection lost");
    }
    // The three MIT-SHM requests, sent the way libxcb-shm sends them, so that the
    // extension needs no library of its own.
    static xcb_extension_t& shmExtension() {
        static xcb_extension_t id{"MIT-SHM", 0};
        return id;
    }
    unsigned shmRequest(uint8_t opcode, void* body, size_t size, bool checked) {
        xcb_protocol_request_t request{2, &shmExtension(), opcode, 1};
        iovec parts[4];
        parts[2].iov_base = body;
        parts[2].iov_len = size;
        parts[3].iov_base = nullptr;
        parts[3].iov_len = (0 - size) & 3;
        return xcb_send_request(connection, checked ? XCB_REQUEST_CHECKED : 0, parts + 2, &request);
    }
    void attachShm(size_t bytes) {
        detachShm();
        if (platform::configEnv("SKRUMI_X11_SHM") == "0") return;
        const auto* extension = xcb_get_extension_data(connection, &shmExtension());
        if (!extension || !extension->present) return;
        int id = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
        if (id < 0) return;
        void* address = shmat(id, nullptr, 0);
        if (address == reinterpret_cast<void*>(-1)) {
            shmctl(id, IPC_RMID, nullptr);
            return;
        }
        uint32_t segment = xcb_generate_id(connection);
        struct { uint8_t major, minor; uint16_t length; uint32_t segment, id; uint8_t readOnly, pad[3]; }
            attach{0, 0, 0, segment, static_cast<uint32_t>(id), 1, {}};
        xcb_void_cookie_t cookie{shmRequest(1, &attach, sizeof attach, true)};
        Reply<xcb_generic_error_t> error(xcb_request_check(connection, cookie), &std::free);
        shmctl(id, IPC_RMID, nullptr); // the memory goes once both sides let go of it
        if (error || xcb_connection_has_error(connection)) {
            shmdt(address);
            return;
        }
        shmSegment = segment;
        shmPixels = static_cast<uint8_t*>(address);
    }
    void detachShm() {
        if (!shmPixels) return;
        struct { uint8_t major, minor; uint16_t length; uint32_t segment; } detach{0, 0, 0, shmSegment};
        shmRequest(2, &detach, sizeof detach, false);
        shmdt(shmPixels);
        shmPixels = nullptr;
        shmSegment = 0;
        shmPending = false;
    }
    // Requests are handled in order: an answer to a later one means the server is done
    // reading the shared frame.
    void waitForServer() {
        Reply<xcb_get_input_focus_reply_t> reply(xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr), &std::free);
    }
    xcb_atom_t atom(const char* name) {
        auto cookie = xcb_intern_atom(connection, 0, static_cast<uint16_t>(std::strlen(name)), name);
        Reply<xcb_intern_atom_reply_t> reply(xcb_intern_atom_reply(connection, cookie, nullptr), &std::free);
        if (!reply) fail("cannot obtain X11 window property");
        return reply->atom;
    }
    // Codes follow Win32 virtual keys. Only letters, digits and space come from their
    // ASCII keysym: the apostrophe keysym is 39, which is also the code of RIGHT.
    static int key(uint32_t symbol) {
        if (symbol >= 'a' && symbol <= 'z') return int(symbol - 'a' + 'A');
        if ((symbol >= 'A' && symbol <= 'Z') || (symbol >= '0' && symbol <= '9') || symbol == ' ') return int(symbol);
        switch (symbol) {
            case 0xff1b: return 27; case 0xff0d: return 13; case 0xff8d: return 13;
            case 0xff51: return 37; case 0xff52: return 38;
            case 0xff53: return 39; case 0xff54: return 40;
            case 0xff09: return 9; case 0xff08: return 8;
            case 0xffff: return 46; case 0xff50: return 36; case 0xff57: return 35;   // Delete, Home, End
            case 0xff55: return 33; case 0xff56: return 34; case 0xff63: return 45;   // Page Up/Down, Insert
            case 0xffe1: case 0xffe2: return 16;                     // Shift
            case 0xffe3: case 0xffe4: return 17;                     // Control
            case 0xffe9: case 0xffea: case 0xff7e: return 18;        // Alt, AltGr
            default: return symbol >= 0xffbe && symbol <= 0xffc9 ? int(symbol - 0xffbe + 112) : 0; // F1..F12
        }
    }
    uint32_t keysymAt(unsigned code, unsigned index) const {
        if (code < minKeycode || index >= keysymsPerCode) return 0;
        size_t at = size_t(code - minKeycode) * keysymsPerCode + index;
        return at < keysyms.size() ? keysyms[at] : 0;
    }
    // The text a key press types. XKB reports the active layout group in bits 13-14 of
    // the core event state, so the Russian layout gives Cyrillic without extra libraries.
    std::string typed(const xcb_key_press_event_t* e) const {
        unsigned state = e->state;
        if (state & (XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | XCB_MOD_MASK_4)) return "";
        unsigned group = (state >> 13) & 3;
        bool shift = state & XCB_MOD_MASK_SHIFT, lock = state & XCB_MOD_MASK_LOCK, numLock = state & XCB_MOD_MASK_2;
        uint32_t plain = keysymAt(e->detail, group * 2), shifted = keysymAt(e->detail, group * 2 + 1);
        if (!plain && !shifted) { plain = keysymAt(e->detail, 0); shifted = keysymAt(e->detail, 1); }
        if (!shifted) shifted = plain;
        uint32_t sym = shift ? shifted : plain;
        if (numLock && shifted >= 0xFFAA && shifted <= 0xFFB9) sym = shift ? plain : shifted;
        uint32_t cp = keysymToUnicode(sym);
        // Caps Lock flips the case of letters only.
        if (lock && cp) {
            uint32_t other = keysymToUnicode(shift ? plain : shifted);
            bool letter = (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= 0x400 && cp <= 0x45F);
            if (letter && other) cp = other;
        }
        return cp >= 0x20 && cp != 0x7F ? utf8(cp) : "";
    }
    void answerSelection(const xcb_selection_request_event_t* request) {
        xcb_selection_notify_event_t reply{};
        reply.response_type = XCB_SELECTION_NOTIFY;
        reply.time = request->time;
        reply.requestor = request->requestor;
        reply.selection = request->selection;
        reply.target = request->target;
        xcb_atom_t property = request->property ? request->property : request->target;
        reply.property = XCB_NONE;
        if (request->selection == clipboardAtom && (!ownedClipboard.empty() || request->target == targetsAtom)) {
            if (request->target == targetsAtom) {
                xcb_atom_t targets[] = {targetsAtom, utf8Atom, XCB_ATOM_STRING};
                xcb_change_property(connection, XCB_PROP_MODE_REPLACE, request->requestor, property, XCB_ATOM_ATOM, 32, 3, targets);
                reply.property = property;
            } else if (request->target == utf8Atom || request->target == XCB_ATOM_STRING) {
                xcb_change_property(connection, XCB_PROP_MODE_REPLACE, request->requestor, property, request->target, 8,
                                    static_cast<uint32_t>(ownedClipboard.size()), ownedClipboard.data());
                reply.property = property;
            }
        }
        xcb_send_event(connection, 0, request->requestor, 0, reinterpret_cast<const char*>(&reply));
        xcb_flush(connection);
    }
    std::vector<Reply<xcb_generic_event_t>> backlog; // events that arrived while waiting for the clipboard
    std::string clipboard() {
        if (!ownedClipboard.empty()) {
            auto owner = Reply<xcb_get_selection_owner_reply_t>(xcb_get_selection_owner_reply(connection, xcb_get_selection_owner(connection, clipboardAtom), nullptr), &std::free);
            if (owner && owner->owner == window) return ownedClipboard;
        }
        xcb_delete_property(connection, window, transferAtom);
        xcb_convert_selection(connection, window, clipboardAtom, utf8Atom, transferAtom, XCB_CURRENT_TIME);
        xcb_flush(connection);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
        bool answered = false;
        while (!answered && std::chrono::steady_clock::now() < deadline) {
            auto* event = xcb_poll_for_event(connection);
            if (!event) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
            if ((event->response_type & 127) == XCB_SELECTION_NOTIFY) {
                auto* notify = reinterpret_cast<xcb_selection_notify_event_t*>(event);
                answered = true;
                bool empty = notify->property == XCB_NONE;
                std::free(event);
                if (empty) return "";
            } else {
                backlog.emplace_back(event, &std::free);
            }
        }
        if (!answered) return "";
        auto cookie = xcb_get_property(connection, 1, window, transferAtom, XCB_GET_PROPERTY_TYPE_ANY, 0, 4 * 1024 * 1024 / 4);
        Reply<xcb_get_property_reply_t> reply(xcb_get_property_reply(connection, cookie, nullptr), &std::free);
        if (!reply || reply->format != 8) return "";
        return std::string(static_cast<const char*>(xcb_get_property_value(reply.get())),
                           static_cast<size_t>(xcb_get_property_value_length(reply.get())));
    }
    void setClipboard(const std::string& text) {
        ownedClipboard = text;
        xcb_set_selection_owner(connection, window, clipboardAtom, XCB_CURRENT_TIME);
        xcb_flush(connection);
    }
    void refreshKeys() {
        const auto* setup = xcb_get_setup(connection);
        uint8_t count = static_cast<uint8_t>(setup->max_keycode - setup->min_keycode + 1);
        auto cookie = xcb_get_keyboard_mapping(connection, setup->min_keycode, count);
        Reply<xcb_get_keyboard_mapping_reply_t> reply(xcb_get_keyboard_mapping_reply(connection, cookie, nullptr), &std::free);
        if (!reply) fail("cannot read X11 keyboard mapping");
        keymap.fill(0);
        const auto* symbols = xcb_get_keyboard_mapping_keysyms(reply.get());
        keysymsPerCode = reply->keysyms_per_keycode;
        minKeycode = setup->min_keycode;
        keysyms.assign(symbols, symbols + size_t(count) * keysymsPerCode);
        // A non-Latin primary layout keeps its Latin keysym in a later group, so WASD
        // must be looked up across all groups instead of group 0 only.
        for (unsigned code = setup->min_keycode; code <= setup->max_keycode; ++code)
            for (unsigned slot = 0; slot < reply->keysyms_per_keycode && !keymap[code]; ++slot)
                keymap[code] = key(symbols[(code - setup->min_keycode) * reply->keysyms_per_keycode + slot]);
    }
    void open(const std::string& title) {
        int screenIndex = 0;
        connection = xcb_connect(nullptr, &screenIndex);
        if (!connection || xcb_connection_has_error(connection)) fail("cannot connect to X11; run in a desktop session with DISPLAY and X11/XWayland");
        const auto* setup = xcb_get_setup(connection);
        auto screens = xcb_setup_roots_iterator(setup);
        for (int i = 0; i < screenIndex && screens.rem; ++i) xcb_screen_next(&screens);
        if (!screens.rem) fail("X11 screen not found");
        auto* screen = screens.data;
        root = screen->root;
        stateAtom = atom("_NET_WM_STATE");
        fullscreenAtom = atom("_NET_WM_STATE_FULLSCREEN");
        depth = screen->root_depth;
        byteOrder = setup->image_byte_order;
        uint8_t pad = 0;
        for (auto formats = xcb_setup_pixmap_formats_iterator(setup); formats.rem; xcb_format_next(&formats))
            if (formats.data->depth == depth) { bits = formats.data->bits_per_pixel; pad = formats.data->scanline_pad; }
        for (auto depths = xcb_screen_allowed_depths_iterator(screen); depths.rem; xcb_depth_next(&depths))
            for (auto visuals = xcb_depth_visuals_iterator(depths.data); visuals.rem; xcb_visualtype_next(&visuals))
                if (visuals.data->visual_id == screen->root_visual && visuals.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR) {
                    red = visuals.data->red_mask; green = visuals.data->green_mask; blue = visuals.data->blue_mask;
                }
        if ((bits != 16 && bits != 24 && bits != 32) || !pad || !red || !green || !blue) fail("X11 display must use a 16/24/32-bit TrueColor visual");
        int width = owner.surface().width(), height = owner.surface().height();
        stride = ((size_t(width) * bits + pad - 1) / pad) * (pad / 8);
        size_t requestBytes = size_t(xcb_get_maximum_request_length(connection)) * 4;
        if (requestBytes <= 64 || stride > requestBytes - 64) fail("X11 maximum image request is too small");
        rowsPerRequest = (requestBytes - 64) / stride;
        attachShm(stride * height);
        if (!shmPixels) image.resize(stride * height);
        window = xcb_generate_id(connection);
        uint32_t values[] = {screen->black_pixel, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
            XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
        check(xcb_create_window_checked(connection, depth, window, screen->root, 0, 0,
            static_cast<uint16_t>(width), static_cast<uint16_t>(height), 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
            XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values));
        protocols = atom("WM_PROTOCOLS"); deleteWindow = atom("WM_DELETE_WINDOW");
        clipboardAtom = atom("CLIPBOARD"); utf8Atom = atom("UTF8_STRING");
        targetsAtom = atom("TARGETS"); transferAtom = atom("SKRUMI_CLIPBOARD");
        check(xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, window, protocols, XCB_ATOM_ATOM, 32, 1, &deleteWindow));
        check(xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, window, atom("_NET_WM_NAME"), atom("UTF8_STRING"), 8, static_cast<uint32_t>(title.size()), title.data()));
        check(xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, static_cast<uint32_t>(title.size()), title.data()));
        sizeHints(width, height);
        back = xcb_generate_id(connection);
        check(xcb_create_pixmap_checked(connection, depth, back, window, static_cast<uint16_t>(width), static_cast<uint16_t>(height)));
        gc = xcb_generate_id(connection);
        uint32_t noExposure = 0;
        check(xcb_create_gc_checked(connection, gc, window, XCB_GC_GRAPHICS_EXPOSURES, &noExposure));
        refreshKeys();
        check(xcb_map_window_checked(connection, window));
        present();
    }
    void poll() {
        std::vector<Reply<xcb_generic_event_t>> events = std::move(backlog);
        backlog.clear();
        while (auto* event = xcb_poll_for_event(connection)) events.emplace_back(event, &std::free);
        for (size_t i = 0; i < events.size(); ++i) {
            auto* event = events[i].get();
            unsigned type = event->response_type & 127;
            if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
                auto* e = reinterpret_cast<xcb_key_press_event_t*>(event);
                // X11 autorepeat emits release+press with the same timestamp. It is not a new press.
                if (type == XCB_KEY_RELEASE && i + 1 < events.size() && (events[i+1]->response_type & 127) == XCB_KEY_PRESS) {
                    auto* next = reinterpret_cast<xcb_key_press_event_t*>(events[i+1].get());
                    if (next->detail == e->detail && next->time == e->time) {
                        ++i;
                        if (keymap[e->detail]) owner.keyEvent(keymap[e->detail], true); // still down: a repeat
                        owner.textEvent(typed(next));
                        continue;
                    }
                }
                if (keymap[e->detail]) owner.keyEvent(keymap[e->detail], type == XCB_KEY_PRESS);
                if (type == XCB_KEY_PRESS) owner.textEvent(typed(e));
            } else if (type == XCB_BUTTON_PRESS || type == XCB_BUTTON_RELEASE) {
                auto* e = reinterpret_cast<xcb_button_press_event_t*>(event);
                owner.mouseEvent(e->event_x, e->event_y);
                if (e->detail == 1 || e->detail == 3) owner.keyEvent(e->detail == 1 ? 1 : 2, type == XCB_BUTTON_PRESS);
                else if (e->detail == 2) owner.keyEvent(4, type == XCB_BUTTON_PRESS);
                else if ((e->detail == 4 || e->detail == 5) && type == XCB_BUTTON_PRESS) owner.wheelEvent(e->detail == 4 ? 1 : -1);
            } else if (type == XCB_MOTION_NOTIFY) {
                auto* e = reinterpret_cast<xcb_motion_notify_event_t*>(event); owner.mouseEvent(e->event_x, e->event_y);
            } else if (type == XCB_CONFIGURE_NOTIFY) {
                auto* e = reinterpret_cast<xcb_configure_notify_event_t*>(event);
                if (e->window == window && e->width > 0 && e->height > 0) owner.sizeEvent(e->width, e->height);
            } else if (type == XCB_PROPERTY_NOTIFY) {
                auto* e = reinterpret_cast<xcb_property_notify_event_t*>(event);
                if (e->atom == stateAtom) {
                    bool wasFull = full;
                    full = hasAtom(window, stateAtom, fullscreenAtom);
                    // Updating hints for unrelated state notifications can trigger a
                    // WM_NORMAL_HINTS / _NET_WM_STATE feedback loop in window managers.
                    if (wasFull && !full) sizeHints(windowedWidth ? windowedWidth : owner.surface().width(),
                                         windowedHeight ? windowedHeight : owner.surface().height());
                    requestedFull = full;
                }
            } else if (type == XCB_FOCUS_IN) owner.focusEvent(true);
            else if (type == XCB_FOCUS_OUT) owner.focusEvent(false);
            else if (type == XCB_MAPPING_NOTIFY) { refreshKeys(); owner.focusEvent(owner.focused()); }
            else if (type == XCB_CLIENT_MESSAGE) {
                auto* e = reinterpret_cast<xcb_client_message_event_t*>(event);
                if (e->type == protocols && e->format == 32 && e->data.data32[0] == deleteWindow) owner.closeEvent();
            } else if (type == XCB_DESTROY_NOTIFY) { window = XCB_NONE; owner.destroyedEvent(); }
            else if (type == XCB_SELECTION_REQUEST) answerSelection(reinterpret_cast<xcb_selection_request_event_t*>(event));
            else if (type == XCB_SELECTION_CLEAR) ownedClipboard.clear();
            else if (type == XCB_EXPOSE) copy();
            else if (type == 0) fail("X11 reported an asynchronous protocol error");
        }
        if (xcb_connection_has_error(connection)) fail("X11 connection lost");
    }
    bool resizable = true;
    void sizeHints(int width, int height, bool forceResizable = false) {
        uint32_t hints[18]{};
        hints[0] = (1 << 4) | (1 << 5); // minimum and maximum size
        hints[5] = static_cast<uint32_t>((resizable || forceResizable) ? 64 : width);
        hints[6] = static_cast<uint32_t>((resizable || forceResizable) ? 64 : height);
        hints[7] = static_cast<uint32_t>((resizable || forceResizable) ? 4096 : width);
        hints[8] = static_cast<uint32_t>((resizable || forceResizable) ? 4096 : height);
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32, 18, hints);
        xcb_flush(connection);
    }
    void setResizable(bool on) {
        resizable = on;
        if (!full && !requestedFull) sizeHints(owner.surface().width(), owner.surface().height());
    }
    void setSize(int width, int height) {
        if (!resizable) sizeHints(width, height);
        uint32_t size[] = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, size);
        xcb_flush(connection);
    }
    // The back buffer and the upload rows follow the new drawing area.
    void resizeBuffers() {
        int width = owner.surface().width(), height = owner.surface().height();
        uint8_t pad = 0;
        for (auto formats = xcb_setup_pixmap_formats_iterator(xcb_get_setup(connection)); formats.rem; xcb_format_next(&formats))
            if (formats.data->depth == depth) pad = formats.data->scanline_pad;
        stride = ((size_t(width) * bits + pad - 1) / pad) * (pad / 8);
        size_t requestBytes = size_t(xcb_get_maximum_request_length(connection)) * 4;
        rowsPerRequest = std::max<size_t>(1, (requestBytes - 64) / stride);
        attachShm(stride * size_t(height));
        if (shmPixels) std::vector<uint8_t>().swap(image);
        else image.assign(stride * size_t(height), 0);
        if (back) xcb_free_pixmap(connection, back);
        back = xcb_generate_id(connection);
        xcb_create_pixmap(connection, depth, back, window, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
    }
    static uint32_t channel(uint32_t value, uint32_t mask) {
        unsigned shift = 0;
        while (!(mask & 1)) { mask >>= 1; ++shift; }
        return static_cast<uint32_t>((uint64_t(value) * mask + 127) / 255) << shift;
    }
    void copy() {
        if (!window) return;
        xcb_copy_area(connection, back, window, gc, 0, 0, 0, 0, static_cast<uint16_t>(owner.surface().width()), static_cast<uint16_t>(owner.surface().height()));
        xcb_flush(connection);
    }
    void present() {
        const auto& surface = owner.surface();
        if (shmPending) {
            waitForServer();
            shmPending = false;
        }
        uint8_t* target = shmPixels ? shmPixels : image.data();
        if (bits == 32 && byteOrder == XCB_IMAGE_ORDER_LSB_FIRST && red == 0xff0000 && green == 0xff00 && blue == 0xff && stride == size_t(surface.width()) * 4) {
            std::memcpy(target, surface.pixels().data(), stride * size_t(surface.height()));
        } else {
            for (int y = 0; y < surface.height(); ++y) for (int x = 0; x < surface.width(); ++x) {
                uint32_t c = surface.pixels()[size_t(y) * surface.width() + x];
                uint32_t pixel = channel((c >> 16) & 255, red) | channel((c >> 8) & 255, green) | channel(c & 255, blue);
                for (unsigned b = 0; b < bits / 8; ++b) target[size_t(y) * stride + size_t(x) * (bits / 8) + b] = static_cast<uint8_t>(pixel >> (8 * (byteOrder == XCB_IMAGE_ORDER_LSB_FIRST ? b : bits / 8 - 1 - b)));
            }
        }
        if (shmPixels) {
            auto width = static_cast<uint16_t>(surface.width()), height = static_cast<uint16_t>(surface.height());
            struct {
                uint8_t major, minor; uint16_t length;
                uint32_t drawable, gc;
                uint16_t totalWidth, totalHeight, sourceX, sourceY, sourceWidth, sourceHeight;
                int16_t x, y;
                uint8_t depth, format, sendEvent, pad;
                uint32_t segment, offset;
            } put{0, 0, 0, back, gc, width, height, 0, 0, width, height, 0, 0, depth, XCB_IMAGE_FORMAT_Z_PIXMAP, 0, 0, shmSegment, 0};
            shmRequest(3, &put, sizeof put, false);
            shmPending = true;
        } else for (size_t y = 0; y < size_t(surface.height()); y += rowsPerRequest) {
            auto rows = std::min(rowsPerRequest, size_t(surface.height()) - y);
            xcb_put_image(connection, XCB_IMAGE_FORMAT_Z_PIXMAP, back, gc, static_cast<uint16_t>(surface.width()), static_cast<uint16_t>(rows), 0, static_cast<int16_t>(y), 0, depth, static_cast<uint32_t>(rows * stride), image.data() + y * stride);
        }
        copy();
        if (xcb_connection_has_error(connection)) fail("X11 connection lost during presentation");
    }
};
#endif

Window::Window(int width, int height, const std::string& title) : surface_(width, height), native_(std::make_unique<Native>(*this)) {
    if (width < 64 || height < 64) fail("window dimensions must be at least 64 pixels");
    if (title.empty() || title.size() > 4096 || title.find('\0') != std::string::npos) fail("window title must contain 1..4096 bytes without NUL");
    native_->open(title);
}
Window::~Window() {
#ifdef _WIN32
    if (mouseCaptured_) ClipCursor(nullptr);
#endif
}
bool Window::poll() {
    mouseDeltaX_ = mouseDeltaY_ = 0;
    pressed_.fill(false);
    repeated_.fill(false);
    text_.clear();
    edits_.clear();
    wheel_ = 0;
    doubleClick_ = false;
    if (!open_) return false;
    auto now = std::chrono::steady_clock::now();
    delta_ = std::clamp(std::chrono::duration<double>(now - lastPoll_).count(), 0.0, 0.1);
    lastPoll_ = now;
    native_->poll();
    resized_ = false;
    if (pendingWidth_ > 0 && pendingHeight_ > 0) {
        applySize(pendingWidth_, pendingHeight_);
        pendingWidth_ = pendingHeight_ = 0;
    }
    if (mouseCaptured_ && focused_) centerMouse();
    surface_.resetClip();
    ui_.beginFrame(*this);
    return open_;
}
void Window::setTargetFps(int fps) {
    if (fps < 0 || fps > 1000) fail("target FPS must be 0 (unlimited) or 1..1000");
    targetFps_ = fps; nextFrame_ = {};
}
void Window::present() {
    if (!open_) return;
    native_->present();
    if (!targetFps_) return;
    auto now = std::chrono::steady_clock::now();
    auto interval = std::chrono::nanoseconds(1000000000LL / targetFps_);
    if (nextFrame_.time_since_epoch().count() == 0 || now > nextFrame_ + interval) nextFrame_ = now + interval;
    if (nextFrame_ > now) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(nextFrame_ - now).count();
        if (ms > 0) platform::pause(ms);
#ifndef __EMSCRIPTEN__
        std::this_thread::sleep_until(nextFrame_);
#endif
    }
    nextFrame_ += interval;
}
void Window::setMouseVisible(bool visible) {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    (void)visible; fail("mouse visibility is currently supported on Windows and X11");
#elif defined(_WIN32)
    mouseVisible_ = visible;
    SetCursor(visible ? LoadCursorW(nullptr, IDC_ARROW) : nullptr);
#else
    auto* c = native_->connection;
    uint32_t cursor = XCB_NONE;
    if (!visible) {
        auto pixmap = xcb_generate_id(c), gc = xcb_generate_id(c);
        cursor = xcb_generate_id(c);
        xcb_create_pixmap(c, 1, pixmap, native_->window, 1, 1);
        uint32_t zero = 0;
        xcb_create_gc(c, gc, pixmap, XCB_GC_FOREGROUND, &zero);
        xcb_rectangle_t rect{0,0,1,1}; xcb_poly_fill_rectangle(c, pixmap, gc, 1, &rect);
        xcb_create_cursor(c, cursor, pixmap, pixmap, 0,0,0,0,0,0,0,0);
        xcb_free_gc(c, gc); xcb_free_pixmap(c, pixmap);
    }
    xcb_change_window_attributes(c, native_->window, XCB_CW_CURSOR, &cursor);
    if (cursor != XCB_NONE) xcb_free_cursor(c, cursor);
    xcb_flush(c); mouseVisible_ = visible;
#endif
}
void Window::centerMouse() {
    int x = surface_.width()/2, y = surface_.height()/2;
#ifdef _WIN32
    POINT point{x,y}; ClientToScreen(native_->hwnd, &point); SetCursorPos(point.x, point.y);
#elif !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    xcb_warp_pointer(native_->connection, XCB_NONE, native_->window, 0,0,0,0, static_cast<int16_t>(x), static_cast<int16_t>(y));
    xcb_flush(native_->connection);
#endif
    mouseX_ = x; mouseY_ = y; mouseKnown_ = true;
}
void Window::setMouseCaptured(bool captured) {
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    (void)captured; fail("mouse capture is currently supported on Windows and X11");
#else
    if (captured && !focused_) fail("mouse capture requires a focused window");
#ifdef _WIN32
    if (captured) {
        RECT rect{}; GetClientRect(native_->hwnd, &rect);
        POINT a{rect.left,rect.top}, b{rect.right,rect.bottom};
        ClientToScreen(native_->hwnd, &a); ClientToScreen(native_->hwnd, &b);
        rect = {a.x,a.y,b.x,b.y}; if (!ClipCursor(&rect)) fail("cannot capture mouse");
    } else ClipCursor(nullptr);
#else
    if (captured) {
        auto cookie = xcb_grab_pointer(native_->connection, 1, native_->window,
            XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, native_->window, XCB_NONE, XCB_CURRENT_TIME);
        auto* reply = xcb_grab_pointer_reply(native_->connection, cookie, nullptr);
        bool ok = reply && reply->status == XCB_GRAB_STATUS_SUCCESS;
        std::free(reply); if (!ok) fail("cannot capture mouse");
    } else xcb_ungrab_pointer(native_->connection, XCB_CURRENT_TIME);
    xcb_flush(native_->connection);
#endif
    mouseCaptured_ = captured;
    if (captured) centerMouse();
    mouseDeltaX_ = mouseDeltaY_ = 0;
#endif
}
void Window::applySize(int width, int height) {
    width = std::clamp(width, 64, 4096);
    height = std::clamp(height, 64, 4096);
    if (int64_t(width) * height > 8388608) height = static_cast<int>(8388608 / width);
    if (width == surface_.width() && height == surface_.height()) return;
    surface_.resize(width, height);
    native_->resizeBuffers();
    resized_ = true;
}
std::array<double, 3> Window::desktopMetrics() const {
#ifdef _WIN32
    // This process uses Windows DPI virtualization: these logical coordinates
    // already account for display scaling. Do not multiply by physical DPI again.
    RECT work{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) fail("cannot query desktop work area");
    return {double(std::max<LONG>(64, work.right - work.left - 32)),
            double(std::max<LONG>(64, work.bottom - work.top - 64)), 1.0};
#elif defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    fail("desktop metrics are unavailable on this platform");
#else
    auto* connection = native_->connection;
    auto root = native_->root;
    Native::Reply<xcb_get_geometry_reply_t> geometry(xcb_get_geometry_reply(connection,
        xcb_get_geometry(connection, root), nullptr), &std::free);
    if (!geometry) fail("cannot query desktop geometry");
    double width = geometry->width, height = geometry->height;
    Native::Reply<xcb_get_property_reply_t> work(xcb_get_property_reply(connection,
        xcb_get_property(connection, 0, root, native_->atom("_NET_WORKAREA"), XCB_ATOM_CARDINAL, 0, 4), nullptr), &std::free);
    if (work && work->format == 32 && work->type == XCB_ATOM_CARDINAL && xcb_get_property_value_length(work.get()) >= 16) {
        auto* values = static_cast<uint32_t*>(xcb_get_property_value(work.get()));
        if (values[2] > 0 && values[3] > 0) {
            width = std::min(width, double(values[2])); height = std::min(height, double(values[3]));
        }
    }
    double scale = 1.0;
    Native::Reply<xcb_get_property_reply_t> resources(xcb_get_property_reply(connection,
        xcb_get_property(connection, 0, root, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 0, 65536), nullptr), &std::free);
    if (resources && resources->format == 8 && resources->type == XCB_ATOM_STRING) {
        std::string text(static_cast<const char*>(xcb_get_property_value(resources.get())),
                         xcb_get_property_value_length(resources.get()));
        auto start = text.find("Xft.dpi:");
        if (start != std::string::npos && (start == 0 || text[start - 1] == '\n')) {
            auto dpi = std::strtod(text.c_str() + start + 8, nullptr);
            if (dpi >= 120 && dpi <= 384) scale = std::clamp(dpi / 96, 1.0, 3.0);
        }
    }
    return {std::max(64.0, width - 32), std::max(64.0, height - 64), scale};
#endif
}
void Window::setResizable(bool resizable) { native_->setResizable(resizable); }
void Window::setFullscreen(bool fullscreen) { native_->setFullscreen(fullscreen); }
bool Window::fullscreen() const { return native_->fullscreen(); }
void Window::setSize(int width, int height) {
    if (fullscreen()) fail("leave fullscreen before setting the window size");
    if (width < 64 || height < 64 || width > 4096 || height > 4096 || int64_t(width) * height > 8388608)
        fail("window size must be 64..4096 on each side and at most 8388608 pixels");
    native_->setSize(width, height);
    applySize(width, height);
}
void Window::keyEvent(int key, bool down) {
    if (key <= 0 || key >= 256) return;
    if (down) {
        // A key that is already down is being repeated by the OS.
        repeated_[key] = true;
        if (key > 2 && key != 4 && edits_.size() < 4096) edits_.push_back({"", key, down_[17]});
        if (!down_[key]) {
            pressed_[key] = true;
            if (key == 1) {
                auto now = std::chrono::steady_clock::now();
                doubleClick_ = now - lastClick_ < std::chrono::milliseconds(450) &&
                               std::abs(mouseX_ - lastClickX_) <= 4 && std::abs(mouseY_ - lastClickY_) <= 4;
                // A third click starts a new pair instead of counting as another double click.
                lastClick_ = doubleClick_ ? std::chrono::steady_clock::time_point{} : now;
                lastClickX_ = mouseX_;
                lastClickY_ = mouseY_;
            }
        }
    }
    down_[key] = down;
}
void Window::focusEvent(bool focused) {
    if (!focused && mouseCaptured_) setMouseCaptured(false);
    focused_ = focused; mouseKnown_ = false;
    mouseDeltaX_ = mouseDeltaY_ = 0;
    down_.fill(false); pressed_.fill(false); repeated_.fill(false);
}
int Window::keyCode(const std::string& key) {
    if (key.size() == 1) {
        unsigned char c = static_cast<unsigned char>(key[0]);
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ') return c;
    }
    if (key == "LEFT") return 37;
    if (key == "UP") return 38;
    if (key == "RIGHT") return 39;
    if (key == "DOWN") return 40;
    if (key == "SPACE") return 32;
    if (key == "ENTER") return 13;
    if (key == "ESCAPE") return 27;
    if (key == "TAB") return 9;
    if (key == "BACKSPACE") return 8;
    if (key == "DELETE") return 46;
    if (key == "HOME") return 36;
    if (key == "END") return 35;
    if (key == "PAGE_UP") return 33;
    if (key == "PAGE_DOWN") return 34;
    if (key == "INSERT") return 45;
    if (key.size() >= 2 && key.size() <= 3 && key[0] == 'F' && key.find_first_not_of("0123456789", 1) == std::string::npos) {
        int number = std::stoi(key.substr(1));
        if (number >= 1 && number <= 12) return 111 + number;
    }
    if (key == "MOUSE_MIDDLE") return 4;
    if (key == "SHIFT") return 16;
    if (key == "CTRL") return 17;
    if (key == "ALT") return 18;
    if (key == "MOUSE_LEFT") return 1;
    if (key == "MOUSE_RIGHT") return 2;
    fail("unknown key '" + key + "'");
}
bool Window::keyDown(const std::string& key) const { return down_[keyCode(key)]; }
bool Window::keyPressed(const std::string& key) const { return pressed_[keyCode(key)]; }
bool Window::keyRepeat(const std::string& key) const { return repeated_[keyCode(key)]; }
std::string Window::clipboard() { return native_->clipboard(); }
void Window::setClipboard(const std::string& text) { native_->setClipboard(text); }
} // namespace skrumi::graphics
