// The WebAssembly Skrumi of the playground page: runs its examples and the limits
// the page relies on. Usage: node test_playground.js <directory with skrumi.js>
const path = require("path");
const fs = require("fs");

const directory = path.resolve(process.argv[2] || ".");
const createSkrumi = require(path.join(directory, "skrumi.js"));

// Runs main.skm among the given files, as the page's worker does: all of them in
// one directory that is also the working directory.
async function run(files, input = "", entry = "main.skm") {
  if (typeof files === "string") files = { "main.skm": files };
  let out = "", err = "", echoed = "";
  const module = await createSkrumi({
    preRun: [(m) => m.FS.init(() => null, (c) => { if (c !== null) out += String.fromCharCode(c); },
                               (c) => { if (c !== null) err += String.fromCharCode(c); })],
    echoInput: (text) => { echoed += text; out += Buffer.from(text, "utf8").toString("latin1"); },
  });
  module.FS.mkdirTree("/project");
  for (const [name, text] of Object.entries(files)) {
    if (name.includes("/")) module.FS.mkdirTree("/project/" + name.slice(0, name.lastIndexOf("/")));
    module.FS.writeFile("/project/" + name, text);
  }
  module.FS.chdir("/project");
  const code = module.ccall("skrumi_run", "number", ["string", "string"], [entry, input]);
  const decode = (text) => Buffer.from(text, "latin1").toString("utf8");
  return { code, out: decode(out), err: decode(err), echoed, module };
}

function expect(condition, message, result) {
  if (!condition) {
    console.error("FAIL: " + message);
    if (result) console.error(JSON.stringify({ code: result.code, out: result.out, err: result.err }, null, 2));
    process.exit(1);
  }
}

(async () => {
  const canonicalModules = await run({"main.skm": 'include("helper.skm"); using extra; print(helper() + extra());',
    "helper.skm": 'int helper() { return 20; }', "extra.skm": 'int extra() { return 22; }'});
  expect(canonicalModules.code === 0 && canonicalModules.out === "42\n", "canonical source imports", canonicalModules);
  expect(canonicalModules.module.ccall("skrumi_version", "string", [], []) ===
    fs.readFileSync(path.join(__dirname, "../../VERSION"), "utf8").trim(),
    "published runtime follows the repository VERSION");
  const unsupported = await run({"main.txt": 'print(42);'}, "", "main.txt");
  expect(unsupported.code === 1 && unsupported.err.includes(".skm"), "reject unsupported source format", unsupported);
  const unsupportedCheck = JSON.parse(unsupported.module.ccall("skrumi_check", "string", ["string"], ["main.txt"]));
  expect(unsupportedCheck.length === 1 && unsupportedCheck[0].severity === "error", "checker rejects unsupported format");

  const examples = path.join(directory, "examples");
  const expected = {
    "hello.skm": "FizzBuzz",
    "expressions.skm": "Выражения и Unicode: всё верно",
    "functions.skm": "fib(25) = 75025",
    "structs.skm": "через год ей будет 6",
    "maps.skm": "Лис в городе: 3",
    "errors.skm": "Поймали ошибку: делить на ноль нельзя",
    "input.skm": "Чисел: 2, сумма: 42",
  };
  const modules = path.join(examples, "modules");
  const multi = await run({
    "main.skm": fs.readFileSync(path.join(modules, "main.skm"), "utf8"),
    "shapes.skm": fs.readFileSync(path.join(modules, "shapes.skm"), "utf8"),
    "tools/report.skm": fs.readFileSync(path.join(modules, "tools/report.skm"), "utf8"),
  });
  expect(multi.code === 0 && multi.out.includes("Общая площадь: 49"), "files find each other through using and include", multi);

  for (const [file, text] of Object.entries(expected)) {
    const result = await run(fs.readFileSync(path.join(examples, file), "utf8"), "Лиса\n12\n30\n");
    expect(result.code === 0 && result.out.includes(text), file, result);
  }

  const echo = await run('string a = input("? ");\nprint("got " + a);', "skrumi\n");
  expect(echo.out === "? skrumi\ngot skrumi\n", "input is echoed where it is read", echo);

  const broken = await run("int x = 1;\nprint(missing);");
  expect(broken.code === 1 && broken.err.includes("main.skm:2"), "errors name their line", broken);

  const deep = await run("int f(int n) { return f(n + 1); }\nf(1);");
  expect(deep.code === 1 && deep.err.includes("call depth limit"), "endless recursion stops before the browser's stack does", deep);

  const window = await run('using graphics;\nopen_window(200, 200, "x");');
  expect(window.code === 1 && window.err.includes("windows are not available here"), "graphics explain themselves", window);

  const problems = JSON.parse(broken.module.ccall("skrumi_check", "string", ["string"], ["main.skm"]));
  expect(problems.length === 1 && problems[0].line === 2 && problems[0].severity === "error", "the checker reports problems");

  // The build for programs with a window: frames reach the page's canvas, keys reach
  // the program while it polls, and errors are still caught.
  const createGraphics = require(path.join(directory, "skrumi-graphics.js"));
  let frames = 0, polls = 0, opened = "", printed = "";
  const graphics = await createGraphics({
    print: (text) => { printed += text + "\n"; },
    printErr: (text) => { printed += text + "\n"; },
    echoInput: () => {},
    canvasOpen: (width, height, title) => { opened = width + "x" + height + " " + title; },
    canvasPresent: (pixels) => { frames++; if (frames === 2) printed += "pixel " + Array.from(pixels.slice(0, 4)) + "\n"; },
    canvasPoll: async () => {
      polls++;
      await new Promise((resolve) => setTimeout(resolve, 0));
      if (polls === 3) graphics._skrumi_key(39, 1);
      if (polls === 5) graphics._skrumi_key(39, 0);
    },
  });
  graphics.FS.mkdirTree("/project");
  graphics.FS.chdir("/project");
  graphics.FS.writeFile("main.skm", `using graphics;
open_window(160, 100, "Тест");
int x = 0;
int n = 0;
while (window_poll() && n < 8) {
    if (key_down("RIGHT")) { x++; }
    clear_window(rgb(255, 128, 0));
    present_window();
    n++;
}
try { throw "ой"; } catch (string e) { print("caught " + e); }
wait(5);
print("x=" + x);
`);
  const windowCode = await graphics.ccall("skrumi_run", "number", ["string", "string"], ["main.skm", ""], { async: true });
  expect(windowCode === 0 && opened === "160x100 Тест", "a window opens on the page's canvas", { code: windowCode, out: printed, err: "" });
  expect(printed.includes("pixel 255,128,0,255"), "frames arrive as RGBA", { code: windowCode, out: printed, err: "" });
  expect(printed.includes("x=2") && printed.includes("caught ой"), "keys arrive while the program polls", { code: windowCode, out: printed, err: "" });

  // Every window example opens, draws and ends when the window is closed.
  for (const file of ["game.skm", "todo.skm", "paint.skm", "raycaster.skm"]) {
    let shownFrames = 0, closePolls = 0, log = "";
    const program = await createGraphics({
      print: (text) => { log += text + "\n"; },
      printErr: (text) => { log += text + "\n"; },
      echoInput: () => {},
      canvasOpen: () => {},
      canvasPresent: () => { shownFrames++; },
      canvasPoll: async () => {
        await new Promise((resolve) => setTimeout(resolve, 0));
        if (++closePolls === 4) program._skrumi_close();
      },
    });
    program.FS.mkdirTree("/project");
    program.FS.chdir("/project");
    program.FS.writeFile("main.skm", fs.readFileSync(path.join(examples, file), "utf8"));
    const exit = await program.ccall("skrumi_run", "number", ["string", "string"], ["main.skm", ""], { async: true });
    expect(exit === 0 && shownFrames >= 3, file + " runs in a window", { code: exit, out: log, err: "" });
  }

  console.log("PLAYGROUND_OK");
})();
