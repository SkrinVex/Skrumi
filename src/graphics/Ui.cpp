#include "Font.h"
#include "skrumi/Unicode.h"
#include "Graphics.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace skrumi::graphics {
namespace {

std::vector<std::string> characters(const std::string& text) { return unicode::graphemes(text); }

std::string join(const std::vector<std::string>& parts, size_t from, size_t to) {
    std::string out;
    for (size_t i = from; i < to && i < parts.size(); ++i) out += parts[i];
    return out;
}

bool inside(int px, int py, int x, int y, int width, int height) {
    return px >= x && py >= y && px < int64_t(x) + width && py < int64_t(y) + height;
}

} // namespace

void Ui::beginFrame(Window& window) {
    previous_ = std::move(current_);
    current_.clear();
    stack_.clear();
    stackModal_.clear();
    clickTaken_ = false;

    // A modal layer seen in the last frame disables every layer drawn before it.
    blockBelow_ = modalLayer_;
    modalLayer_ = 0;
    nextLayer_ = 0;

    hot_.clear();
    hotLayer_ = -1;
    for (const auto& region : previous_) {
        if (region.layer < blockBelow_) continue;
        if (!inside(window.mouseX(), window.mouseY(), region.x, region.y, region.width, region.height)) continue;
        if (region.layer >= hotLayer_) {
            hotLayer_ = region.layer;
            hot_ = region.id;
        }
    }

    // A held element keeps the mouse until the button goes up.
    if (!drag_.empty()) {
        auto held = std::find_if(previous_.begin(), previous_.end(), [&](const Region& r) { return r.id == drag_; });
        if (!window.keyDown("MOUSE_LEFT") || held == previous_.end() || held->layer < blockBelow_) {
            drag_.clear();
        } else {
            hot_ = drag_;
            hotLayer_ = held->layer;
        }
    }

    // The last scroll area drawn under the mouse is the innermost one.
    previousScrolls_ = std::move(scrolls_);
    scrolls_.clear();
    wheelOwner_.clear();
    for (const auto& area : previousScrolls_) {
        if (area.layer < blockBelow_ || area.layer < hotLayer_) continue;
        if (inside(window.mouseX(), window.mouseY(), area.x, area.y, area.width, area.height)) wheelOwner_ = area.id;
    }

    bool fresh = focusFresh_;
    focusFresh_ = false;
    if (!focus_.empty()) {
        auto owner = std::find_if(previous_.begin(), previous_.end(), [&](const Region& r) { return r.id == focus_; });
        // The field is gone, hidden behind a modal, or the click went somewhere else.
        // Inside a dialog only another field takes the keyboard away; elsewhere any click
        // outside the field does.
        auto hot = std::find_if(previous_.begin(), previous_.end(), [&](const Region& r) { return r.id == hot_; });
        bool hotIsField = hot != previous_.end() && hot->focusable;
        bool clickedAway = window.keyPressed("MOUSE_LEFT") && hot_ != focus_ && (blockBelow_ == 0 || hotIsField);
        bool missing = owner == previous_.end() && !fresh;
        if (missing || (owner != previous_.end() && owner->layer < blockBelow_) || clickedAway) {
            focus_.clear();
        } else if (owner != previous_.end() && window.keyPressed("TAB")) {
            std::vector<const Region*> ring;
            for (const auto& region : previous_)
                if (region.focusable && region.layer == owner->layer) ring.push_back(&region);
            auto at = std::find_if(ring.begin(), ring.end(), [&](const Region* r) { return r->id == focus_; });
            if (ring.size() > 1 && at != ring.end()) {
                size_t index = static_cast<size_t>(at - ring.begin());
                index = window.keyDown("SHIFT") ? (index + ring.size() - 1) % ring.size() : (index + 1) % ring.size();
                focus_ = ring[index]->id;
                field(focus_).cursor = static_cast<size_t>(-1); // caret to the end of the newly focused text
            }
        }
    }
}

void Ui::layerBegin(bool modal) {
    stack_.push_back(++nextLayer_);
    stackModal_.push_back(modal);
    if (modal) modalLayer_ = nextLayer_;
}

void Ui::layerEnd() {
    if (stack_.empty()) throw std::runtime_error("Graphics Error: ui layer ended without a matching begin");
    stack_.pop_back();
    stackModal_.pop_back();
}

Ui::Region Ui::add(const std::string& id, int x, int y, int width, int height, bool focusable, Window& window) {
    if (id.empty()) throw std::runtime_error("Graphics Error: an interface element needs a non-empty id");
    // Asking about the same element twice in a frame (clicked, then hovered) is one element.
    for (auto it = current_.rbegin(); it != current_.rend() && it - current_.rbegin() < 8; ++it)
        if (it->id == id && it->layer == layer()) return *it;
    // Only the visible part of an element inside a scroll area can be hit.
    auto area = window.surface().clip();
    int left = static_cast<int>(std::clamp<int64_t>(x, area.left, area.right));
    int top = static_cast<int>(std::clamp<int64_t>(y, area.top, area.bottom));
    int right = static_cast<int>(std::clamp<int64_t>(int64_t(x) + std::max(0, width), left, area.right));
    int bottom = static_cast<int>(std::clamp<int64_t>(int64_t(y) + std::max(0, height), top, area.bottom));
    Region region{id, left, top, right - left, bottom - top, layer(), focusable};
    if (current_.size() < 100000) current_.push_back(region);
    return region;
}

// Under the mouse and not covered: the topmost element of the last frame, or, for an
// element that just appeared, any element whose layer is not disabled by a modal.
bool Ui::active(const Region& region, Window& window) const {
    if (!inside(window.mouseX(), window.mouseY(), region.x, region.y, region.width, region.height)) return false;
    if (layer() < blockBelow_ || layer() < modalLayer_) return false;
    return hot_ == region.id;
}

bool Ui::hover(const std::string& id, int x, int y, int width, int height, Window& window) {
    Region region = add(id, x, y, width, height, false, window);
    if (!drag_.empty()) return drag_ == id;
    return active(region, window);
}

bool Ui::click(const std::string& id, int x, int y, int width, int height, Window& window) {
    Region region = add(id, x, y, width, height, false, window);
    // One click, one element: the button that opens a dialog does not also press
    // whatever the dialog shows at the same place in the same frame.
    if (clickTaken_ || !window.keyPressed("MOUSE_LEFT") || !active(region, window)) return false;
    clickTaken_ = true;
    return true;
}

bool Ui::drag(const std::string& id, int x, int y, int width, int height, Window& window) {
    Region region = add(id, x, y, width, height, false, window);
    if (drag_.empty() && !clickTaken_ && window.keyPressed("MOUSE_LEFT") && active(region, window)) {
        clickTaken_ = true;
        drag_ = id;
        dragX_ = window.mouseX() - x;
        dragY_ = window.mouseY() - y;
    }
    return drag_ == id && window.keyDown("MOUSE_LEFT");
}

int Ui::wheel(const std::string& id, int x, int y, int width, int height, Window& window) {
    if (id.empty()) throw std::runtime_error("Graphics Error: an interface element needs a non-empty id");
    auto area = window.surface().clip();
    int left = std::max(area.left, x), top = std::max(area.top, y);
    int right = static_cast<int>(std::min<int64_t>(area.right, int64_t(x) + std::max(0, width)));
    int bottom = static_cast<int>(std::min<int64_t>(area.bottom, int64_t(y) + std::max(0, height)));
    if (scrolls_.size() < 10000) scrolls_.push_back({id, left, top, std::max(0, right - left), std::max(0, bottom - top), layer(), false});
    if (window.wheel() == 0 || layer() < blockBelow_ || layer() < modalLayer_) return 0;
    if (!inside(window.mouseX(), window.mouseY(), left, top, right - left, bottom - top)) return 0;
    // An area that just appeared may take the wheel only while no known area claims it.
    bool known = std::any_of(previousScrolls_.begin(), previousScrolls_.end(), [&](const Region& r) { return r.id == id; });
    if (wheelOwner_ == id || (!known && wheelOwner_.empty() && layer() >= hotLayer_)) {
        wheelOwner_ = id;
        return window.wheel();
    }
    return 0;
}

Ui::Field& Ui::field(const std::string& id) {
    for (auto& entry : fields_) if (entry.first == id) return entry.second;
    if (fields_.size() > 4096) fields_.erase(fields_.begin());
    fields_.push_back({id, {}});
    return fields_.back().second;
}

std::string Ui::text(const std::string& id, int x, int y, int width, int height, const std::string& value,
                     int scale, uint32_t color, Window& window, Font* font, int pixels) {
    if (scale < 1 || scale > 32) throw std::runtime_error("Graphics Error: text scale must be 1..32");
    Region region = add(id, x, y, width, height, true, window);
    auto chars = characters(value);
    Field& state = field(id);
    const int pad = 4 * scale;
    int available = std::max(1, width - 2 * pad);
    std::vector<int> offsets;
    auto measure = [&]() {
        offsets.clear(); offsets.push_back(0);
        for (const auto& ch : chars) offsets.push_back(offsets.back() + (font ? font->width(ch, pixels) : Surface::textWidth(ch, scale) + scale));
    };
    measure();
    state.scroll = std::min(state.scroll, chars.size());

    if (!clickTaken_ && window.keyPressed("MOUSE_LEFT") && active(region, window)) {
        clickTaken_ = true;
        focus_ = id;
        int position = window.mouseX() - x - pad + offsets[state.scroll];
        state.selectedAll = false;
        state.cursor = state.scroll;
        while (state.cursor < chars.size() && position > (offsets[state.cursor] + offsets[state.cursor+1]) / 2) ++state.cursor;
    }
    state.cursor = std::min(state.cursor, chars.size());

    // Keyboard events reach only the window they belong to, so focus inside it is enough.
    // A field focused by ui_focus in this frame skips the frame's typing: the key that
    // opened a dialog (a letter shortcut) must not also land in the dialog's field.
    bool editing = focus_ == id;
    if (editing && !focusFresh_) {
        auto eraseSelection = [&]() {
            if (!state.selectedAll) return false;
            chars.clear(); state.cursor = 0; state.scroll = 0; state.selectedAll = false;
            return true;
        };
        auto resegment = [&]() {
            // Separate OS text events can extend an existing cluster (combining
            // accents, skin tones and ZWJ). Re-segment the full edited text.
            size_t caretByte = join(chars, 0, state.cursor).size();
            std::string combined = join(chars, 0, chars.size());
            auto boundaries = unicode::graphemeBoundaries(combined);
            state.cursor = static_cast<size_t>(std::lower_bound(boundaries.begin(), boundaries.end(), caretByte) - boundaries.begin());
            chars = characters(combined);
        };
        auto insert = [&](const std::string& typed) {
            if (!typed.empty()) eraseSelection();
            for (auto& ch : characters(typed)) {
                if (ch.find_first_of("\n\r\t") != std::string::npos) continue;
                if (chars.size() >= 65536) break;
                chars.insert(chars.begin() + static_cast<std::ptrdiff_t>(state.cursor), ch);
                ++state.cursor;
            }
            resegment();
        };
        // Replay the frame's typing in order: "x", Backspace, "y" gives "y".
        for (const auto& step : window.edits()) {
            if (!step.text.empty()) {
                if (!step.control) insert(step.text);
                continue;
            }
            switch (step.key) {
                case 8: if (!eraseSelection() && state.cursor > 0) chars.erase(chars.begin() + static_cast<std::ptrdiff_t>(--state.cursor)); break;
                case 46: if (!eraseSelection() && state.cursor < chars.size()) chars.erase(chars.begin() + static_cast<std::ptrdiff_t>(state.cursor)); break;
                case 37: if (state.selectedAll) state.cursor = 0; else if (state.cursor > 0) --state.cursor; state.selectedAll = false; break;
                case 39: if (state.selectedAll) state.cursor = chars.size(); else if (state.cursor < chars.size()) ++state.cursor; state.selectedAll = false; break;
                case 36: state.selectedAll = false; state.cursor = 0; break;
                case 35: state.selectedAll = false; state.cursor = chars.size(); break;
                case 'A': if (step.control) { state.selectedAll = true; state.cursor = chars.size(); } break;
                case 'X': if (step.control && state.selectedAll) { window.setClipboard(join(chars, 0, chars.size())); eraseSelection(); } break;
                case 'V': if (step.control) insert(window.clipboard()); break;
                case 'C': if (step.control) window.setClipboard(join(chars, 0, chars.size())); break;
                default: break;
            }
            if (step.key == 8 || step.key == 46) resegment();
        }
    }

    // Keep the caret inside the visible part of a long text.
    measure();
    if (state.cursor < state.scroll) state.scroll = state.cursor;
    while (state.scroll < state.cursor && offsets[state.cursor] - offsets[state.scroll] > available) ++state.scroll;
    if (state.scroll > chars.size()) state.scroll = chars.size();
    size_t visible = 0;
    while (state.scroll + visible < chars.size() && offsets[state.scroll+visible+1] - offsets[state.scroll] <= available) ++visible;

    int textHeight = font ? pixels + 3 : 7 * scale;
    int textY = y + (height - textHeight) / 2;
    if (editing && state.selectedAll) {
        window.surface().rectangle(x + pad, textY - scale,
            offsets[state.scroll+visible] - offsets[state.scroll], textHeight + 2, color);
        color ^= 0x00ffffff;
    }
    if (font) font->draw(window.surface(), x + pad, textY, join(chars, state.scroll, state.scroll + visible), pixels, color);
    else window.surface().text(x + pad, textY, join(chars, state.scroll, state.scroll + visible), scale, color);
    // The caret blinks twice a second while the field has the keyboard.
    auto phase = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (editing && (phase / 500) % 2 == 0) {
        int caretX = x + pad + (offsets[state.cursor] - offsets[state.scroll]) - std::max(1, scale / 2);
        window.surface().rectangle(caretX, textY - scale, std::max(1, scale), textHeight + 2, color);
    }
    return join(chars, 0, chars.size());
}

} // namespace skrumi::graphics
