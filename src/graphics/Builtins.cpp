#include "Font.h"
#include "skrumi/Unicode.h"
#include "Builtins.h"
#include "Graphics.h"
#include "skrumi/Skrumi.h"
#include "skrumi/Runtime.h"
#include "skrumi/Platform.h"
#include "../core/builtins/Builtin.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace skrumi::graphics {
namespace {
using runtime::Call;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("Graphics Error: " + message); }

int number(const Value& value) {
    if (value.isInt() && value.asInt() >= std::numeric_limits<int>::min() && value.asInt() <= std::numeric_limits<int>::max())
        return static_cast<int>(value.asInt());
    double result = 0;
    if (!runtime::tryNumber(value, result)) fail("expected a number, got '" + value.typeName() + "'");
    if (!std::isfinite(result) || result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max())
        fail("numeric argument out of range");
    return static_cast<int>(result);
}
int integer(const Call& c, size_t i) { return number(c.args[i]); }
uint32_t checkedColor(int value) {
    if (value < 0 || value > 0xffffff) fail("color must be 0..16777215");
    return static_cast<uint32_t>(value);
}
uint32_t color(const Call& c, size_t i) { return checkedColor(integer(c, i)); }
int level(const Call& c, size_t i, const char* what) {
    int value = integer(c, i);
    if (value < 0 || value > 255) fail(std::string(what) + " must be 0..255");
    return value;
}
const std::string& id(const Call& c, size_t i) {
    if (c.args[i].str().empty()) fail("an interface element needs a non-empty id");
    return c.args[i].str();
}

Window& window(Call& c) {
    auto& slot = c.ctx.getRoot()->graphics;
    if (!slot) fail("call open_window first");
    return *slot;
}
Fonts& fonts(Call& c) {
    auto& slot = c.ctx.getRoot()->fonts;
    if (!slot) slot = std::make_shared<Fonts>();
    return *slot;
}
Canvases& canvases(Call& c) {
    auto& slot = c.ctx.getRoot()->canvases;
    if (!slot) slot = std::make_shared<Canvases>();
    return *slot;
}
Surface& surface(Call& c) {
    auto& targets = c.ctx.getRoot()->canvases;
    return targets && targets->selected ? targets->get(targets->selected) : window(c).surface();
}

// Pictures live apart from the window: they can be loaded before it opens.
std::vector<std::shared_ptr<Image>>& images() {
    static std::vector<std::shared_ptr<Image>> loaded;
    return loaded;
}
Image& picture(const Call& c, size_t i) {
    int handle = integer(c, i);
    auto& loaded = images();
    if (handle < 1 || size_t(handle) > loaded.size() || !loaded[size_t(handle) - 1])
        fail(std::to_string(handle) + " is not a loaded image");
    return *loaded[size_t(handle) - 1];
}
Value decodePicture(const std::string& bytes) {
    auto image = std::make_shared<Image>(decodeImage(bytes));
    auto& loaded = images();
    for (size_t slot = 0; slot < loaded.size(); ++slot)
        if (!loaded[slot]) {
            loaded[slot] = image;
            return Value::integer(static_cast<long long>(slot + 1));
        }
    loaded.push_back(image);
    return Value::integer(static_cast<long long>(loaded.size()));
}
Value loadImage(const std::string& path) {
    std::ifstream file(platform::pathFromUtf8(path), std::ios::binary);
    if (!file) fail("cannot open image '" + path + "'");
    std::stringstream bytes; bytes << file.rdbuf();
    if (file.bad()) fail("cannot read image '" + path + "'");
    return decodePicture(bytes.str());
}

uint32_t pixel(const Call& c) {
    Image& image = picture(c, 0);
    int x = integer(c, 1), y = integer(c, 2);
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) fail("pixel is outside the image");
    return image.pixels[size_t(y) * image.width + x];
}

// The numbers of a batch, checked to come in whole groups.
const ValueList& groups(Call& c, size_t i, size_t size, const char* what) {
    const auto& items = c.array(i);
    if (items.size() % size != 0)
        fail(std::string(what) + " must hold groups of " + std::to_string(size) + " numbers, got " + std::to_string(items.size()));
    return items;
}

// An atlas cut into equal frames, numbered left to right and top to bottom from 0.
struct Atlas {
    Image& image;
    int frameWidth, frameHeight, columns, count;
    Atlas(Image& picture, int width, int height) : image(picture), frameWidth(width), frameHeight(height) {
        if (width <= 0 || height <= 0) fail("frame size must be positive");
        columns = picture.width / width;
        count = columns * (picture.height / height);
        if (count == 0) fail("the image is smaller than one frame");
    }
    void draw(Surface& surface, int frame, int x, int y) const {
        if (frame >= count) fail("frame " + std::to_string(frame) + " is outside the atlas of " + std::to_string(count));
        surface.image(image, (frame % columns) * frameWidth, (frame / columns) * frameHeight, frameWidth, frameHeight,
                      x, y, frameWidth, frameHeight, 255);
    }
};

void drawRects(Call& c) {
    Surface& s = surface(c);
    const auto& items = groups(c, 0, 5, "rects");
    for (size_t i = 0; i < items.size(); i += 5)
        s.rectangle(number(items[i]), number(items[i + 1]), number(items[i + 2]), number(items[i + 3]), checkedColor(number(items[i + 4])));
}

void drawSprites(Call& c) {
    Surface& s = surface(c);
    Atlas atlas(picture(c, 0), integer(c, 1), integer(c, 2));
    const auto& items = groups(c, 3, 3, "sprites");
    for (size_t i = 0; i < items.size(); i += 3) {
        int frame = number(items[i]);
        if (frame < 0) fail("frame numbers start at 0, got " + std::to_string(frame));
        atlas.draw(s, frame, number(items[i + 1]), number(items[i + 2]));
    }
}

void drawTiles(Call& c) {
    Surface& s = surface(c);
    Atlas atlas(picture(c, 0), integer(c, 1), integer(c, 2));
    const auto& map = c.array(3);
    int columns = integer(c, 4);
    if (columns <= 0) fail("a tile map needs at least one column");
    int64_t x = integer(c, 5), y = integer(c, 6);
    size_t rows = (map.size() + size_t(columns) - 1) / size_t(columns);
    // Only the tiles that reach into the drawing area are looked at.
    Surface::Clip area = s.clip();
    auto first = [](int64_t edge, int64_t origin, int64_t step) { return std::max<int64_t>(0, (edge - origin) / step); };
    int64_t rowFrom = first(area.top, y, atlas.frameHeight), colFrom = first(area.left, x, atlas.frameWidth);
    for (int64_t row = rowFrom; row < int64_t(rows); ++row) {
        int64_t top = y + row * atlas.frameHeight;
        if (top >= area.bottom) break;
        for (int64_t col = colFrom; col < columns; ++col) {
            size_t at = size_t(row) * size_t(columns) + size_t(col);
            if (at >= map.size()) break;
            int64_t left = x + col * atlas.frameWidth;
            if (left >= area.right) break;
            int tile = number(map[at]);
            if (tile >= 0) atlas.draw(s, tile, static_cast<int>(left), static_cast<int>(top));
        }
    }
}
ValueList missingText(Call& c) {
    int handle = integer(c, 0);
    Font* font = handle == 0 ? nullptr : &fonts(c).get(handle);
    ValueList missing;
    std::vector<std::string> seen;
    for (const auto& cluster : unicode::graphemes(c.text(1))) {
        size_t offset = 0;
        char32_t cp = unicode::next(cluster, offset);
        bool supported = offset == cluster.size() && (font ? font->supports(cp) : Surface::hasGlyph(cp));
        if (!supported && std::find(seen.begin(), seen.end(), cluster) == seen.end()) {
            seen.push_back(cluster); missing.push_back(Value::string(cluster));
        }
    }
    return missing;
}
} // namespace

const std::vector<Signature>& signatures() {
    static const std::vector<Signature> result = {
        {"gfx_canvas_create", "create_canvas", "int", {{"int","width"},{"int","height"}}, "Создать RGB canvas без окна; максимум 32 canvas, 32 миллиона пикселей суммарно.",
         [](Call& c) { return Value::integer(canvases(c).create(integer(c,0),integer(c,1))); }},
        {"gfx_canvas_select", "draw_to_canvas", "void", {{"int","canvas"}}, "Выбрать canvas для рисования; 0 возвращает рисование в окно.",
         [](Call& c) { canvases(c).select(integer(c,0)); return Value(); }},
        {"gfx_canvas_free", "free_canvas", "void", {{"int","canvas"}}, "Освободить canvas; активная цель сбрасывается на окно.",
         [](Call& c) { canvases(c).release(integer(c,0)); return Value(); }},
        {"gfx_canvas_draw", "draw_canvas", "void", {{"int","canvas"},{"int","x"},{"int","y"}}, "Копировать RGB canvas в текущую цель.",
         [](Call& c) { auto& src = canvases(c).get(integer(c,0)); Image image{src.width(),src.height(),src.pixels()}; for (auto& p : image.pixels) p |= 0xff000000U; surface(c).image(image,0,0,image.width,image.height,integer(c,1),integer(c,2),image.width,image.height,255); return Value(); }},
        {"gfx_canvas_png", "canvas_png", "string", {{"int","canvas"}}, "PNG-байты canvas без сжатия; можно сохранить через fs_write_atomic.",
         [](Call& c) { return Value::string(canvases(c).get(integer(c,0)).png()); }},
        {"gfx_screenshot", "save_screenshot", "void", {{"string","path"}}, "Сохранить RGB framebuffer окна в PNG, без декораций окна.",
         [](Call& c) { auto bytes = window(c).surface().png(); std::ofstream out(platform::pathFromUtf8(c.text(0)),std::ios::binary); out.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); out.close(); if (!out) fail("cannot save screenshot"); return Value(); }},
        {"gfx_polygon", "draw_polygon", "void", {{"array","points"},{"int","color"}}, "Заполненный многоугольник: [x,y,x,y,...], 3..4096 вершин, правило even-odd.",
         [](Call& c) { const auto& values = groups(c,0,2,"points"); if (values.size() < 6 || values.size() > 8192) fail("polygon needs 3..4096 points"); std::vector<std::array<int,2>> points; for (size_t i=0;i<values.size();i+=2) points.push_back({number(values[i]),number(values[i+1])}); surface(c).polygon(points,color(c,1)); return Value(); }},
        {"gfx_ellipse", "draw_ellipse", "void", {{"int","x"},{"int","y"},{"int","radius_x"},{"int","radius_y"},{"int","color"}}, "Заполненный эллипс с радиусами 0..8192.",
         [](Call& c) { surface(c).ellipse(integer(c,0),integer(c,1),integer(c,2),integer(c,3),color(c,4)); return Value(); }},
        {"gfx_gradient", "draw_gradient", "void", {{"int","x"},{"int","y"},{"int","width"},{"int","height"},{"int","from"},{"int","to"},{"bool","vertical"}}, "RGB-градиент; vertical=true сверху вниз, false слева направо.",
         [](Call& c) { surface(c).gradient(integer(c,0),integer(c,1),integer(c,2),integer(c,3),color(c,4),color(c,5),c.flag(6)); return Value(); }},
        {"gfx_image_ex", "draw_image_ex", "void", {{"int","image"},{"int","x"},{"int","y"},{"float","scale"},{"float","angle"},{"bool","flip_x"},{"bool","flip_y"},{"int","alpha"}}, "Поворот вокруг центра масштабированной картинки; angle в радианах по часовой стрелке, alpha 0..255.",
         [](Call& c) { auto& img=picture(c,0); surface(c).imageEx(img,0,0,img.width,img.height,integer(c,1),integer(c,2),c.number(3),c.number(4),c.flag(5),c.flag(6),integer(c,7)); return Value(); }},
        {"gfx_image_part_ex", "draw_image_part_ex", "void", {{"int","image"},{"int","sx"},{"int","sy"},{"int","sw"},{"int","sh"},{"int","x"},{"int","y"},{"float","scale"},{"float","angle"},{"bool","flip_x"},{"bool","flip_y"},{"int","alpha"}}, "Часть атласа с масштабом, поворотом вокруг центра и отражением; angle в радианах, alpha 0..255.",
         [](Call& c) { surface(c).imageEx(picture(c,0),integer(c,1),integer(c,2),integer(c,3),integer(c,4),integer(c,5),integer(c,6),c.number(7),c.number(8),c.flag(9),c.flag(10),integer(c,11)); return Value(); }},
        {"gfx_font_text_alpha", "draw_font_text_alpha", "void", {{"int","font"},{"int","x"},{"int","y"},{"string","text"},{"int","pixels"},{"int","color"},{"int","alpha"}}, "TTF-текст с непрозрачностью alpha 0..255.",
         [](Call& c) { fonts(c).get(integer(c,0)).draw(surface(c),integer(c,1),integer(c,2),c.text(3),integer(c,4),color(c,5),level(c,6,"alpha")); return Value(); }},
        {"gfx_circles", "draw_circles", "void", {{"array","circles"}}, "Пакет кругов: [x,y,radius,color,...].",
         [](Call& c) { auto& s=surface(c); const auto& v=groups(c,0,4,"circles"); for(size_t i=0;i<v.size();i+=4) s.circle(number(v[i]),number(v[i+1]),number(v[i+2]),checkedColor(number(v[i+3]))); return Value(); }},
        {"gfx_lines", "draw_lines", "void", {{"array","lines"}}, "Пакет линий: [x1,y1,x2,y2,color,...].",
         [](Call& c) { auto& s=surface(c); const auto& v=groups(c,0,5,"lines"); for(size_t i=0;i<v.size();i+=5) s.line(number(v[i]),number(v[i+1]),number(v[i+2]),number(v[i+3]),checkedColor(number(v[i+4]))); return Value(); }},

        {"gfx_circle_alpha", "draw_circle_alpha", "void", {{"int","x"},{"int","y"},{"int","radius"},{"int","color"},{"int","alpha"}}, "Рисует с прозрачностью alpha 0..255.",
         [](Call& c) { surface(c).circle(integer(c, 0), integer(c, 1), integer(c, 2), color(c, 3), integer(c, 4)); return Value(); }},
        {"gfx_line_alpha", "draw_line_alpha", "void", {{"int","x1"},{"int","y1"},{"int","x2"},{"int","y2"},{"int","color"},{"int","alpha"}}, "Рисует с прозрачностью alpha 0..255.",
         [](Call& c) { surface(c).line(integer(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), color(c, 4), integer(c, 5)); return Value(); }},
        {"gfx_ring_alpha", "draw_ring_alpha", "void", {{"int","x"},{"int","y"},{"int","radius"},{"int","thickness"},{"int","color"},{"int","alpha"}}, "Рисует с прозрачностью alpha 0..255.",
         [](Call& c) { surface(c).ring(integer(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), color(c, 4), integer(c, 5)); return Value(); }},
        {"gfx_text_alpha", "draw_text_alpha", "void", {{"int","x"},{"int","y"},{"string","text"},{"int","scale"},{"int","color"},{"int","alpha"}}, "Рисует с прозрачностью alpha 0..255.",
         [](Call& c) { surface(c).text(integer(c, 0), integer(c, 1), c.text(2), integer(c, 3), color(c, 4), integer(c, 5)); return Value(); }},
        {"gfx_font_fallback", "font_with_fallback", "int", {{"int","body"},{"int","fallback"}}, "Новый font handle с цепочкой fallback; исходные handles можно освободить.",
         [](Call& c) { return Value::integer(fonts(c).withFallback(integer(c, 0), integer(c, 1))); }},
        {"gfx_text_supported", "text_supported", "bool", {{"int","font"},{"string","text"}}, "Проверка текста без окна: font 0 — встроенный шрифт; составные графемы без shaping считаются неподдержанными.",
         [](Call& c) { return Value::boolean(missingText(c).empty()); }},
        {"gfx_text_missing", "text_missing_glyphs", "array", {{"int","font"},{"string","text"}}, "Уникальные неподдержанные графемы; font 0 — встроенный шрифт.",
         [](Call& c) { return runtime::makeArray(missingText(c)); }},
        {"gfx_target_fps", "set_target_fps", "void", {{"int","fps"}}, "Ограничение FPS при present_window; 0 — без ограничения, 1..1000.",
         [](Call& c) { window(c).setTargetFps(integer(c, 0)); return Value(); }},
        {"gfx_mouse_visible", "set_mouse_visible", "void", {{"bool","visible"}}, "Скрыть/показать системный курсор внутри окна Windows/X11.",
         [](Call& c) { window(c).setMouseVisible(c.flag(0)); return Value(); }},
        {"gfx_mouse_captured", "set_mouse_captured", "void", {{"bool","captured"}}, "Захват мыши с относительным движением Windows/X11; автоматически снимается при потере фокуса.",
         [](Call& c) { window(c).setMouseCaptured(c.flag(0)); return Value(); }},
        {"gfx_mouse_delta", "mouse_delta", "array", {}, "Относительное движение [dx, dy] за последний window_poll, в пикселях.",
         [](Call& c) { auto& w = window(c); return runtime::makeArray({Value::integer(w.mouseDeltaX()), Value::integer(w.mouseDeltaY())}); }},
        {"gfx_open", "open_window", "void", {{"int","width"},{"int","height"},{"string","title"}}, "Открыть нативное графическое окно, изменяемое по размеру по умолчанию. Одно окно на интерпретатор, 64..4096 пикселей по каждой оси, не более 8388608 пикселей.",
         [](Call& c) { auto& slot = c.ctx.getRoot()->graphics; if (slot) fail("close the existing window before opening another"); int width = integer(c, 0), height = integer(c, 1);
             bool fullscreen = false;
             if (c.ctx.interpreter) {
                 const auto& options = c.ctx.interpreter->getOptions();
                 if (options.windowWidth) width = options.windowWidth;
                 if (options.windowHeight) height = options.windowHeight;
                 fullscreen = options.windowFullscreen;
             }
             auto opened = std::make_shared<Window>(width, height, c.args[2].str());
             if (fullscreen) opened->setFullscreen(true);
             slot = std::move(opened); return Value(); }},
        {"gfx_poll", "window_poll", "bool", {}, "Обработать события кадра. Возвращает false после закрытия окна. Вызывать один раз в начале кадра.",
         [](Call& c) { return Value::boolean(window(c).poll()); }},
        {"gfx_close", "close_window", "void", {}, "Закрыть окно и освободить ресурсы. Повторный вызов допустим.",
         [](Call& c) { c.ctx.getRoot()->graphics.reset(); return Value(); }},
        {"gfx_intercept_close", "intercept_window_close", "void", {{"bool","enabled"}}, "Перехватывать запрос закрытия; window_poll продолжает возвращать true. Отключение при ожидающем запросе закрывает окно.",
         [](Call& c) { window(c).interceptClose(c.args[0].asBool()); return Value(); }},
        {"gfx_close_requested", "window_close_requested", "bool", {}, "Есть ожидающий запрос закрытия. Повторные запросы объединяются до cancel_window_close или close_window.",
         [](Call& c) { return Value::boolean(window(c).closeRequested()); }},
        {"gfx_cancel_close", "cancel_window_close", "void", {}, "Отменить ожидающий запрос закрытия окна.",
         [](Call& c) { window(c).cancelClose(); return Value(); }},
        {"gfx_clear", "clear_window", "void", {{"int","color"}}, "Очистить буфер цветом rgb(r,g,b).",
         [](Call& c) { Surface& s = surface(c); s.clear(color(c, 0)); return Value(); }},
        {"gfx_rect", "draw_rect", "void", {{"int","x"},{"int","y"},{"int","width"},{"int","height"},{"int","color"}}, "Нарисовать заполненный прямоугольник с отсечением по границам окна.",
         [](Call& c) { Surface& s = surface(c); s.rectangle(integer(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), color(c, 4)); return Value(); }},
        {"gfx_circle", "draw_circle", "void", {{"int","x"},{"int","y"},{"int","radius"},{"int","color"}}, "Нарисовать заполненный круг. Радиус 0..8192.",
         [](Call& c) { Surface& s = surface(c); s.circle(integer(c, 0), integer(c, 1), integer(c, 2), color(c, 3)); return Value(); }},
        {"gfx_text", "draw_text", "void", {{"int","x"},{"int","y"},{"string","text"},{"int","scale"},{"int","color"}}, "Встроенный пиксельный шрифт: латиница, русский алфавит, цифры. Строчные и прописные буквы. Масштаб 1..32.",
         [](Call& c) { Surface& s = surface(c); s.text(integer(c, 0), integer(c, 1), c.args[2].str(), integer(c, 3), color(c, 4)); return Value(); }},
        {"gfx_present", "present_window", "void", {}, "Показать буфер кадра в нативном окне.",
         [](Call& c) { window(c).present(); return Value(); }},
        {"gfx_delta", "frame_delta", "float", {}, "Время между window_poll в секундах, ограниченное 0.1 секунды.",
         [](Call& c) { return runtime::realResult(window(c).delta()); }},
        {"gfx_down", "key_down", "bool", {{"string","key"}}, "Проверить удержание клавиши: A..Z, 0..9, LEFT/RIGHT/UP/DOWN, SPACE/ENTER/ESCAPE/TAB/BACKSPACE, SHIFT/CTRL/ALT, MOUSE_LEFT/MOUSE_RIGHT.",
         [](Call& c) { return Value::boolean(window(c).keyDown(c.args[0].str())); }},
        {"gfx_pressed", "key_pressed", "bool", {{"string","key"}}, "Проверить новое нажатие в текущем кадре; не сбрасывается при повторном чтении.",
         [](Call& c) { return Value::boolean(window(c).keyPressed(c.args[0].str())); }},
        {"gfx_mouse_x", "mouse_x", "int", {}, "Положение мыши по X относительно окна.",
         [](Call& c) { return Value::integer(window(c).mouseX()); }},
        {"gfx_mouse_y", "mouse_y", "int", {}, "Положение мыши по Y относительно окна.",
         [](Call& c) { return Value::integer(window(c).mouseY()); }},
        {"gfx_focused", "window_focused", "bool", {}, "Имеет ли окно фокус. Потеря фокуса сбрасывает удерживаемые клавиши.",
         [](Call& c) { return Value::boolean(window(c).focused()); }},
        {"gfx_line", "draw_line", "void", {{"int","x1"},{"int","y1"},{"int","x2"},{"int","y2"},{"int","color"}}, "Нарисовать отрезок толщиной в пиксель между двумя точками.",
         [](Call& c) { Surface& s = surface(c); s.line(integer(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), color(c, 4)); return Value(); }},
        {"gfx_frame", "draw_frame", "void", {{"int","x"},{"int","y"},{"int","width"},{"int","height"},{"int","thickness"},{"int","color"}}, "Нарисовать контур прямоугольника заданной толщины.",
         [](Call& c) { Surface& s = surface(c); s.frame(integer(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), integer(c, 4), color(c, 5)); return Value(); }},
        {"gfx_ring", "draw_ring", "void", {{"int","x"},{"int","y"},{"int","radius"},{"int","thickness"},{"int","color"}}, "Нарисовать окружность (кольцо) заданной толщины.",
         [](Call& c) { Surface& s = surface(c); s.ring(integer(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), color(c, 4)); return Value(); }},
        {"gfx_text_width", "text_width", "int", {{"string","text"},{"int","scale"}}, "Ширина текста в пикселях при данном масштабе — для выравнивания по центру. Окно не требуется.",
         [](Call& c) { return Value::integer(Surface::textWidth(c.args[0].str(), integer(c, 1))); }},
        {"gfx_text_input", "text_input", "string", {}, "Текст, набранный с прошлого кадра: учитывает раскладку (в том числе русскую), Shift, Caps Lock и автоповтор. Для полей ввода.",
         [](Call& c) { return Value::string(window(c).text()); }},
        {"gfx_repeat", "key_repeat", "bool", {{"string","key"}}, "Клавиша нажата в этом кадре или повторяется системой при удержании — для Backspace, стрелок и прокрутки списков.",
         [](Call& c) { return Value::boolean(window(c).keyRepeat(c.args[0].str())); }},
        {"gfx_wheel", "mouse_wheel", "int", {}, "Поворот колеса мыши с прошлого кадра в щелчках: больше нуля — от себя (вверх), меньше — на себя.",
         [](Call& c) { return Value::integer(window(c).wheel()); }},
        {"gfx_double_click", "double_clicked", "bool", {}, "Был ли в этом кадре двойной щелчок левой кнопкой (две нажатия быстрее 0.45 с на одном месте).",
         [](Call& c) { return Value::boolean(window(c).doubleClicked()); }},
        {"gfx_clipboard", "clipboard_text", "string", {}, "Текст из системного буфера обмена или пустая строка.",
         [](Call& c) { return Value::string(window(c).clipboard()); }},
        {"gfx_set_clipboard", "set_clipboard_text", "void", {{"string","text"}}, "Помещает текст в системный буфер обмена.",
         [](Call& c) { window(c).setClipboard(c.args[0].str()); return Value(); }},
        {"gfx_rect_alpha", "draw_rect_alpha", "void", {{"int","x"},{"int","y"},{"int","width"},{"int","height"},{"int","color"},{"int","alpha"}}, "Полупрозрачный прямоугольник: alpha 0 — не виден, 255 — непрозрачный. Для затемнения фона под диалогом.",
         [](Call& c) { Surface& s = surface(c); s.blend(integer(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), color(c, 4), level(c, 5, "alpha")); return Value(); }},
        {"gfx_width", "window_width", "int", {}, "Ширина области рисования окна в пикселях.",
         [](Call& c) { return Value::integer(window(c).surface().width()); }},
        {"gfx_height", "window_height", "int", {}, "Высота области рисования окна в пикселях.",
         [](Call& c) { return Value::integer(window(c).surface().height()); }},
        {"gfx_fullscreen", "window_fullscreen", "bool", {}, "Полноэкранный режим окна. На X11 состояние подтверждается оконным менеджером при window_poll.",
         [](Call& c) { return Value::boolean(window(c).fullscreen()); }},
        {"gfx_set_fullscreen", "set_window_fullscreen", "void", {{"bool","fullscreen"}}, "Переключить полноэкранный режим без смены разрешения монитора. При выходе восстанавливается положение и размер окна. Только Windows/X11.",
         [](Call& c) { window(c).setFullscreen(c.args[0].asBool()); return Value(); }},
        {"gfx_resizable", "set_window_resizable", "void", {{"bool","resizable"}}, "Разрешает (true, по умолчанию) или запрещает менять размер окна мышью. Размер области рисования следует за окном: 64..4096 точек по каждой оси.",
         [](Call& c) { window(c).setResizable(c.args[0].asBool()); return Value(); }},
        {"gfx_set_size", "set_window_size", "void", {{"int","width"},{"int","height"}}, "Меняет размер обычного окна и области рисования; сначала выйдите из fullscreen. Существующие пиксели в пересекающейся области сохраняются. 64..4096 по каждой оси, не более 8388608 пикселей.",
         [](Call& c) { Window& w = window(c); w.setSize(integer(c, 0), integer(c, 1)); return Value(); }},
        {"gfx_desktop_metrics", "desktop_metrics", "array", {}, "После открытия окна: [доступная ширина, высота, рекомендуемый масштаб]. Windows: логические DPI-координаты; X11: work area и Xft.dpi. Browser/Android: явная ошибка.",
         [](Call& c) { auto values = window(c).desktopMetrics(); return runtime::makeArray({Value::real(values[0]), Value::real(values[1]), Value::real(values[2])}); }},
        {"gfx_resized", "window_resized", "bool", {}, "Изменился ли размер окна с прошлого кадра — пора заново расставить элементы.",
         [](Call& c) { return Value::boolean(window(c).resized()); }},
        {"gfx_ui_text_font", "ui_text_field", "string", {{"string","id"},{"int","x"},{"int","y"},{"int","width"},{"int","height"},{"string","text"},{"int","font"},{"int","pixels"},{"int","color"}}, "Редактируемое поле с масштабируемым шрифтом; тот же ввод/фокус/выделение, что gfx_ui_text. Размер 8..96 пикселей.",
         [](Call& c) { auto& font = fonts(c).get(integer(c, 6)); font.width("", integer(c, 7)); return Value::string(window(c).ui().text(id(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), integer(c, 4), c.args[5].str(), 1, color(c, 8), window(c), &font, integer(c, 7))); }},
        {"gfx_font_load", "load_font", "int", {{"string","bytes"}}, "Загружает масштабируемый Unicode-шрифт из памяти (до 8 МиБ); до 32 font handle на runtime. Ошибка формата — Graphics Error.",
         [](Call& c) { return Value::integer(fonts(c).load(c.args[0].str())); }},
        {"gfx_font_free", "free_font", "void", {{"int","font"}}, "Освобождает font handle. Шрифты также освобождаются при уничтожении runtime.",
         [](Call& c) { fonts(c).release(integer(c, 0)); return Value(); }},
        {"gfx_font_width", "font_text_width", "int", {{"int","font"},{"string","text"},{"int","pixels"}}, "Ширина текста шрифтом; размер 8..96 пикселей, без shaping/kerning. Переносы строк поддерживаются.",
         [](Call& c) { return Value::integer(fonts(c).get(integer(c, 0)).width(c.args[1].str(), integer(c, 2))); }},
        {"gfx_font_text", "draw_font_text", "void", {{"int","font"},{"int","x"},{"int","y"},{"string","text"},{"int","pixels"},{"int","color"}}, "Сглаженный текст, x/y — верх строки, размер 8..96 пикселей. Общий рендерер Windows/Linux/Android/Web; требуется окно.",
         [](Call& c) { fonts(c).get(integer(c, 0)).draw(surface(c), integer(c, 1), integer(c, 2), c.args[3].str(), integer(c, 4), color(c, 5)); return Value(); }},
        {"gfx_image_decode", "decode_image", "int", {{"string","bytes"}}, "Декодирует PNG/BMP из байтов строки, включая resource_read; без временных файлов. Ошибка формата — Graphics Error.",
         [](Call& c) { return decodePicture(c.args[0].str()); }},
        {"gfx_image_load", "load_image", "int", {{"string","path"}}, "Загружает картинку PNG или BMP и возвращает её номер для рисования. Окно не требуется. Поддерживаются все виды PNG, в том числе с прозрачностью.",
         [](Call& c) { return loadImage(c.args[0].str()); }},
        {"gfx_image_width", "image_width", "int", {{"int","image"}}, "Ширина загруженной картинки в пикселях.",
         [](Call& c) { return Value::integer(picture(c, 0).width); }},
        {"gfx_image_height", "image_height", "int", {{"int","image"}}, "Высота загруженной картинки в пикселях.",
         [](Call& c) { return Value::integer(picture(c, 0).height); }},
        {"gfx_image_draw", "draw_image_scaled", "void", {{"int","image"},{"int","x"},{"int","y"},{"int","width"},{"int","height"},{"int","opacity"}}, "Рисует картинку в прямоугольнике (растягивая по ближайшему пикселю) с её прозрачностью и общей непрозрачностью 0..255.",
         [](Call& c) { Surface& s = surface(c); int opacity = level(c, 5, "opacity"); Image& image = picture(c, 0); s.image(image, 0, 0, image.width, image.height, integer(c, 1), integer(c, 2), integer(c, 3), integer(c, 4), opacity); return Value(); }},
        {"gfx_image_draw_part", "draw_image_part", "void", {{"int","image"},{"int","source_x"},{"int","source_y"},{"int","source_width"},{"int","source_height"},{"int","x"},{"int","y"},{"int","width"},{"int","height"}}, "Рисует часть картинки — кадр спрайта или плитку атласа — в прямоугольнике окна.",
         [](Call& c) { Surface& s = surface(c); s.image(picture(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), integer(c, 4), integer(c, 5), integer(c, 6), integer(c, 7), integer(c, 8), 255); return Value(); }},
        {"gfx_image_pixel", "image_pixel", "int", {{"int","image"},{"int","x"},{"int","y"}}, "Цвет пикселя картинки как rgb(r, g, b) — например, для карты столкновений.",
         [](Call& c) { return Value::integer(pixel(c) & 0xFFFFFF); }},
        {"gfx_image_alpha", "image_alpha", "int", {{"int","image"},{"int","x"},{"int","y"}}, "Непрозрачность пикселя картинки: 0 — прозрачный, 255 — сплошной.",
         [](Call& c) { return Value::integer(pixel(c) >> 24); }},
        {"gfx_image_free", "free_image", "void", {{"int","image"}}, "Освобождает память картинки; её номер больше не действует.",
         [](Call& c) { picture(c, 0); images()[size_t(integer(c, 0)) - 1].reset(); return Value(); }},
        {"gfx_clip_begin", "clip_begin", "void", {{"int","x"},{"int","y"},{"int","width"},{"int","height"}}, "Дальше рисование и щелчки по элементам интерфейса ограничены прямоугольником (внутри уже открытого). Сбрасывается в начале кадра.",
         [](Call& c) { Surface& s = surface(c); s.pushClip(integer(c, 0), integer(c, 1), integer(c, 2), integer(c, 3)); return Value(); }},
        {"gfx_clip_end", "clip_end", "void", {}, "Возвращает прямоугольник рисования, действовавший до clip_begin.",
         [](Call& c) { surface(c).popClip(); return Value(); }},
        {"gfx_ui_layer_begin", "ui_layer_begin", "void", {{"bool","modal"}}, "Начинает слой интерфейса поверх нарисованного раньше. Модальный слой отключает все элементы ниже него: клики и ввод до них не доходят.",
         [](Call& c) { window(c).ui().layerBegin(c.args[0].asBool()); return Value(); }},
        {"gfx_ui_layer_end", "ui_layer_end", "void", {}, "Закрывает слой, начатый ui_layer_begin.",
         [](Call& c) { window(c).ui().layerEnd(); return Value(); }},
        {"gfx_ui_hover", "ui_hover", "bool", {{"string","id"},{"int","x"},{"int","y"},{"int","width"},{"int","height"}}, "Регистрирует элемент интерфейса и возвращает true, если мышь над ним и его не закрывает элемент выше или модальный слой.",
         [](Call& c) { Window& w = window(c); return Value::boolean(w.ui().hover(id(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), integer(c, 4), w)); }},
        {"gfx_ui_click", "ui_click", "bool", {{"string","id"},{"int","x"},{"int","y"},{"int","width"},{"int","height"}}, "Как ui_hover, но true только в кадре щелчка. Один щелчок достаётся одному элементу — верхнему.",
         [](Call& c) { Window& w = window(c); return Value::boolean(w.ui().click(id(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), integer(c, 4), w)); }},
        {"gfx_ui_text", "ui_text_content", "string", {{"string","id"},{"int","x"},{"int","y"},{"int","width"},{"int","height"},{"string","text"},{"int","scale"},{"int","color"}}, "Содержимое однострочного поля ввода: щелчок даёт полю фокус и ставит курсор, набор текста, Backspace/Delete, стрелки, Home/End, Ctrl+V и Ctrl+C. Рисует текст и мигающий курсор, возвращает новый текст. Рамку рисует вызывающий код (см. ui_text_field в using ui;).",
         [](Call& c) { Window& w = window(c); return Value::string(w.ui().text(id(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), integer(c, 4), c.args[5].str(), integer(c, 6), color(c, 7), w)); }},
        {"gfx_ui_focused", "ui_focused", "bool", {{"string","id"}}, "Есть ли у элемента фокус клавиатуры.",
         [](Call& c) { return Value::boolean(window(c).ui().focused(c.args[0].str())); }},
        {"gfx_ui_focus", "ui_focus", "void", {{"string","id"}}, "Передаёт фокус клавиатуры элементу; пустая строка снимает фокус.",
         [](Call& c) { window(c).ui().setFocus(c.args[0].str()); return Value(); }},
        {"gfx_ui_typing", "ui_typing", "bool", {}, "Идёт ли ввод в поле: пока true, горячие клавиши-буквы приложения обрабатывать не стоит.",
         [](Call& c) { return Value::boolean(window(c).ui().typing()); }},
        {"gfx_ui_drag", "ui_drag", "bool", {{"string","id"},{"int","x"},{"int","y"},{"int","width"},{"int","height"}}, "Элемент, который тянут мышью: true от нажатия на нём до отпускания кнопки, даже если мышь ушла за его край. Пока его тянут, другие элементы не подсвечиваются.",
         [](Call& c) { Window& w = window(c); return Value::boolean(w.ui().drag(id(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), integer(c, 4), w)); }},
        {"gfx_ui_drag_x", "ui_drag_x", "int", {}, "На каком расстоянии от левого края перетаскиваемого элемента его схватили.",
         [](Call& c) { return Value::integer(window(c).ui().dragX()); }},
        {"gfx_ui_drag_y", "ui_drag_y", "int", {}, "На каком расстоянии от верхнего края перетаскиваемого элемента его схватили.",
         [](Call& c) { return Value::integer(window(c).ui().dragY()); }},
        {"gfx_ui_wheel", "ui_wheel", "int", {{"string","id"},{"int","x"},{"int","y"},{"int","width"},{"int","height"}}, "Поворот колеса для прокручиваемой области: достаётся самой внутренней области под мышью, не закрытой модальным окном или слоем выше; остальным 0.",
         [](Call& c) { Window& w = window(c); return Value::integer(w.ui().wheel(id(c, 0), integer(c, 1), integer(c, 2), integer(c, 3), integer(c, 4), w)); }},
        {"gfx_rects", "draw_rects", "void", {{"array","rects"}}, "Рисует много заполненных прямоугольников одним вызовом. Массив — подряд идущие пятёрки чисел x, y, ширина, высота, цвет.",
         [](Call& c) { drawRects(c); return Value(); }},
        {"gfx_sprites", "draw_sprites", "void", {{"int","image"},{"int","frame_width"},{"int","frame_height"},{"array","sprites"}}, "Рисует много кадров из картинки-атласа одним вызовом. Атлас делится на кадры frame_width × frame_height, они нумеруются слева направо и сверху вниз с 0. Массив — подряд идущие тройки: номер кадра, x, y.",
         [](Call& c) { drawSprites(c); return Value(); }},
        {"gfx_tiles", "draw_tiles", "void", {{"int","image"},{"int","tile_width"},{"int","tile_height"},{"array","tiles"},{"int","columns"},{"int","x"},{"int","y"}}, "Рисует карту плиток из атласа одним вызовом: tiles — номера плиток по строкам, columns плиток в строке, левый верхний угол в (x, y). Номер меньше 0 — пустая клетка. Плитки за краем окна не рисуются.",
         [](Call& c) { drawTiles(c); return Value(); }},
        {"gfx_rgb", "rgb", "int", {{"int","red"},{"int","green"},{"int","blue"}}, "Создать цвет 0xRRGGBB. Каждый канал должен быть в диапазоне 0..255.",
         [](Call& c) { int r = integer(c, 0), g = integer(c, 1), b = integer(c, 2); if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) fail("RGB channels must be 0..255"); return Value::integer((r << 16) | (g << 8) | b); }}
    };
    return result;
}
} // namespace skrumi::graphics

namespace skrumi::runtime {
void addGraphicsBuiltins(std::vector<Builtin>& out) {
    for (const auto& signature : graphics::signatures()) {
        BuiltinSpec spec{signature.builtin, signature.result, signature.params, signature.params.size(),
                         false, "graphics", signature.documentation};
        out.push_back({std::move(spec), signature.handler});
    }
}
} // namespace skrumi::runtime
