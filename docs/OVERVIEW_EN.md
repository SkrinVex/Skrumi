# Skrumi: a practical overview

Skrumi is a C-like language for desktop applications, small servers and Android.
It combines native 2D graphics and UI, files, JSON and HTTP(S) with an LSP, debugger,
tests, a browser playground and a visual UI builder. Source files use `.skm`.

The C++17 interpreter compiles source to bytecode and executes it on a register VM.
Standalone builds bundle the interpreter and source; the source remains extractable.
The same runtime embeds in C++ and Kotlin/Java. Building APKs requires Android
SDK/NDK/Gradle; platform APIs have different availability in the browser and native builds.

[Download Windows/Linux installers](https://github.com/SkrinVex/Skrumi/releases/latest) ·
[Try in the browser](https://skrinvex.github.io/Skrumi/playground/) ·
[UI builder](https://skrinvex.github.io/Skrumi/builder/).
The complete reference is currently [in Russian](../DOCUMENTATION.md); this page
is an English introduction, not a full translation.

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

Declared types are checked at runtime; `check` and LSP detect some errors in advance,
but do not prove that a program is type-correct. Assignment, parameters and returns
allow implicit int-to-float widening and int/float/bool-to-string conversion.
Float-to-int narrowing requires `to_int`, even for `2.0`; assigning a numeric string
to a number also requires explicit conversion. Nullable values use `T?`.
Operators have additional rules: string `+` concatenates, numeric strings can take
part in arithmetic (`"3" * 2` is `6`) and mixed comparisons (`1 == "1"` is true).
Neither rule allows `int n = "3"`.

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

Published [benchmarks and methodology](../benchmarks/README.md) compare eight
small programs, using the best of five process runs and matching output checksums.
They are measurements on a specified machine, not a general performance guarantee.
The release guard normally rejects regressions exceeding both 10% and 30 ms;
the manual `allow_slower` option records a deliberate exception.
