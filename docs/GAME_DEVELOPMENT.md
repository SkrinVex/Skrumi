# Игровые приёмы

Пример использует дельту времени, ограничение FPS, полупрозрачность и headless-
пригодную геометрию. Файл пользователя `moon_forest.skm` в репозитории отсутствует;
этот пример не подменяет исходную игру.

```cpp
using graphics;
using game;
open_window(640, 400, "Лунный лес: частицы");
set_target_fps(60);
array<Particle> particles = particle_pool(128);
Vec2 hero = Vec2(320.0, 200.0);
while (window_poll()) {
    float dt = frame_delta();
    if (window_focused()) {
        hero.x += (key_down("RIGHT") ? 1.0 : 0.0) * 120.0 * dt;
        hero.x -= (key_down("LEFT") ? 1.0 : 0.0) * 120.0 * dt;
        if (key_pressed("SPACE")) {
            spawn_particle(particles, hero.x, hero.y, 0.0, -40.0, 1.0, 0xFF8800, 8);
        }
        update_particles(particles, dt);
    }
    clear_window(0x101828);
    draw_circle(to_int(hero.x), to_int(hero.y), 10, 0x88CCFF);
    draw_particles(particles);
    draw_text_alpha(12, 12, "ЛУННЫЙ ЛЕС • ★", 2, 0xFFFFFF, 220);
    present_window();
}
close_window();
```

Для плавного движения храните координаты как float и округляйте при рисовании.
`frame_delta` ограничен 0.1 с; для быстрых объектов применяйте фиксированные
подшаги физики. Пауза должна пропускать обновление мира, сохраняя обработку ввода
и рисование меню. При потере фокуса захват мыши снимается; для возобновления
захвата дождитесь фокуса и действия игрока.

`using game;` предоставляет `Vec2`, `Rect`, `Circle`, `Camera2D`, `Timer`,
`Interval`, `Tween`, `Particle`. Таймеры получают delta явно и не зависят от
реального времени. Это позволяет тестировать игровые правила без окна:

<!-- skrumi-run -->
```cpp
using game;
Timer cooldown;
cooldown.start(0.5);
assert(!cooldown.update(0.2));
assert(cooldown.update(0.3));
assert(!cooldown.update(0.1));
assert(circles_collide(Circle(0.0, 0.0, 2.0), Circle(3.0, 0.0, 1.0)));
```

Для тестирования большой игры держите объявления отдельно от запуска:
`include("logic.skm")` загружает объявления, не выполняя верхнеуровневый цикл.
Именуйте тесты `*_test.skm`, либо передайте файл явно. `assert_close` нужен для
вещественных расчётов; фиксированный `random_seed` делает спавн воспроизводимым.

Микшер `play_sound_ex(path,SFX,0.8,false)` позволяет выстрелу и попаданию звучать
одновременно; музыку запускайте с `MUSIC` и loop=true. Пауза игры не останавливает
аудио автоматически: сохраните handle и вызовите pause_sound/resume_sound.
Ошибки отсутствующего устройства можно перехватить try/catch и продолжить без звука.

`save_json("moon-forest-highscore.json", data)` пишет атомарно в user_data_dir;
`load_json` выбрасывает исключение, если файла нет или JSON повреждён.
Для структуры нужна явная реконструкция из map после json_decode.

Структуры и массивы передаются по ссылке: `Particle p = particles[i]` меняет
исходную частицу. Для независимых данных используйте `copy`. `const` защищает
привязку имени и не замораживает содержимое массива.

Для многих спрайтов используйте `draw_sprites`, для карты — `draw_tiles`,
для прямоугольников — `draw_rects`. Заранее выделяйте pool и загружайте ресурсы
до цикла. Универсального числа «допустимых вызовов на кадр» нет: измеряйте
целевое устройство, разрешение и сцену, не сравнивайте только число строк кода.

Готовый самостоятельный пример: [moon_grove.skm](../examples/moon_grove.skm).
Он использует кэшированный canvas леса, градиенты, alpha-свечение, частицы,
фиксированный seed, паузу при потере фокуса и ограничитель FPS. Управление:
WASD/стрелки, Space — искры, P — пауза, Esc — выход. Для автопрогона можно
задать SKRUMI_DEMO_FRAMES и SKRUMI_DEMO_SCREENSHOT. Это отдельный пример,
не копия отсутствующей исходной игры пользователя.
