# Skrumi: a practical overview

Skrumi is a typed, C-like language with a bytecode VM, native 2D graphics,
Unicode strings, modules, a standard library, LSP, debugger and standalone builds.
The complete reference is currently [in Russian](../DOCUMENTATION.md).

```sh
skrumi init my-game
cd my-game
skrumi run .
skrumi check --project
skrumi test
skrumi build . --force
```

Conditions require bool. The conditional operator is lazy; bitwise operators use
signed 32-bit integers, and right shift preserves the sign. Hex, binary, numeric
separators, exponents and non-nested block comments are supported. Fields and
array/map elements support postfix increment and decrement.

<!-- skrumi-run -->
```cpp
int mask = 0b1000 | (1 << 2);
string label = (mask & 4) != 0 ? "ready" : "waiting";
assert_equal(label, "ready");
assert_equal(grapheme_length("🦊🇷🇺👨‍👩‍👧‍👦"), 3);
random_seed(42);
array order = shuffle([1, 2, 3]);
assert_equal(size(order), 3);
```

Assignment shares arrays, maps and structs; `copy` makes a deep copy. Const binds
a name and does not freeze a container. `size` and `byte_size` count UTF-8 bytes;
`str_length`/`char_length` count code points; `grapheme_length` counts extended
grapheme clusters. Grapheme-aware editing does not imply emoji rendering.

Graphics includes alpha primitives, TTF fallback, coverage checks, transformed
sprites, RGB offscreen canvases and PNG screenshots. Color emoji and complex-script
shaping are not implemented; use licensed PNG icon atlases. Native audio exposes
handles, channel/master volume, looping, pause, pan and fades. See the
[platform limitations](AUDIO.md), especially static-musl builds and Web audio.

Read [project tools](PROJECTS.md), [fonts and Unicode](FONTS.md),
[game development](GAME_DEVELOPMENT.md), [diagnostics](DIAGNOSTICS.md), and the
[implemented/planned feature matrix](DEVELOPMENT_STATUS.md).
