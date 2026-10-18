"""Execute runtime, analyzer, standalone and LSP language contracts."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="skrumi-contracts-") as temp:
    root = Path(temp)
    def run(code, check=False, ok=True):
        source = root / "contract.skm"
        source.write_text(code, encoding="utf-8")
        p = subprocess.run([str(binary), *(["check"] if check else []), str(source)],
                           cwd=root, capture_output=True, text=True, timeout=30)
        assert (p.returncode == 0) == ok, (code, p.stdout, p.stderr)
        return p
    for code in [
        "int x = 2.0;", "int x = min(2.0, 3);", "int x = 1; x = 2.5;", "int x = 1; x += 2.5;",
        "int f() { return 2.5; } f();", "void f(int x) {} f(2.0);",
        "array<int> x = [1, 2.5];", "map<string,int> x = {a: 2.0};",
        "array<array<int>> x = [[2.0]];",
        "struct P { int x; } P p = P(2.5);",
        "struct P { int x = 2.5; } P p;",
        "struct P { int x; } P p; p.x = 2.5;",
        "array<int> x = [1]; x[0] = 2.5;", "resize([], 2.5);",
    ]:
        for check in [False, True]:
            p = run(code, check=check, ok=False)
            if check: assert ":0:0:" not in p.stdout+p.stderr, p
            assert "to_int()" in p.stdout+p.stderr or "must be int" in p.stdout+p.stderr, p
    for code in [
        "void f(x) { int y = x; } f(2.0);",
        "func f = (int x) => x; f(2.5);",
        "array<int> x; push(x, 2.5);",
        'map<string,int> x; set(x, "a", 2.5);',
        "array x = [1]; print(x[0.5]);",
        "func resize_array = resize; resize_array([], 2.0);",
    ]:
        run(code, ok=False)
    for code in ["array x 3;", "array<int> x (2 + 1);", "array x -1;"]:
        for check in [False, True]:
            p = run(code, check=check, ok=False)
            assert "resize" in p.stdout+p.stderr, p
    p = run('using server as http; http.get("/users", 123);', ok=False)
    assert "must be a function or its name" in p.stderr, p
    positive = """
array<int> zeros; resize(zeros, 3);
assert_equal(zeros, [0,0,0]);
assert_equal(to_int(-2.9), -2);
float wide = 2147483647;
assert_equal(wide, 2147483647.0);
array<int> numbers = [to_int(2.9)];
assert_equal(numbers, [2]);
int size(x) { return 77; }
assert_equal(size([]), 77);
func chosen = size;
assert_equal(chosen("x"), 77);
void call_dynamic(x) { assert_equal(size(x), 77); }
call_dynamic([]);
"""
    run(positive, check=True)
    run(positive)
    code = "int size(a,b) { return 7; } size([]);"
    run(code, check=True, ok=False)
    run(code, ok=False)
    (root/"routes.skm").write_text("int get(path, handler) { return 42; }")
    run('using routes as routes; int get(a,b) { return 99; } assert_equal(routes.get("/", 1), 42); assert_equal(get([], 0), 99);')
    source = root/"contract.skm"
    source.write_text(positive)
    app = root/("app.exe" if os.name == "nt" else "app")
    p = subprocess.run([str(binary), "build", str(source), "-o", str(app)], cwd=root, capture_output=True, text=True, timeout=60)
    assert p.returncode == 0, p
    p = subprocess.run([str(app)], cwd=root, capture_output=True, text=True, timeout=30)
    assert p.returncode == 0, p
    messages = [
        dict(jsonrpc="2.0", id=1, method="initialize", params={}),
        dict(jsonrpc="2.0", method="textDocument/didOpen", params=dict(textDocument=dict(uri=source.as_uri(), languageId="skrumi", version=1, text="int x = 2.5;"))),
        dict(jsonrpc="2.0", id=2, method="shutdown"),
        dict(jsonrpc="2.0", method="exit"),
    ]
    wire = b""
    for message in messages:
        body = json.dumps(message).encode()
        wire += f"Content-Length: {len(body)}\r\n\r\n".encode()+body
    p = subprocess.run([str(binary.with_name("skrumi-lsp"+binary.suffix))], input=wire, capture_output=True, timeout=30)
    assert p.returncode == 0 and b"to_int()" in p.stdout, p
print("LANGUAGE_CONTRACTS_OK")
