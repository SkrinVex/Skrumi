"""Short programs whose whole behaviour is pinned down: what they print, the errors
they report with file and line, and their exit code, as recorded in
test_programs.json. Run with --update to record the current behaviour after a
deliberate change; the diff of the JSON file then shows what changed."""
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

EXPECTED = Path(__file__).with_name("test_programs.json")

CASES = {
    # Errors and the lines they are reported at
    "undefined": "int a = 1;\nint b = missing + a;\n",
    "in_function": "int f(int x) {\n    int y = x * 2;\n    return y / 0;\n}\nprint(1);\nprint(f(3));\n",
    "nested_call_line": "int inner() {\n  fail(\"deep\");\n  return 1;\n}\nint outer() {\n  int v = inner();\n  return v;\n}\nprint(outer());\n",
    "after_return_line": "int f() { return 1; }\nint a = f();\nint b = a / 0;\n",
    "condition_type": "int i = 0;\nwhile (i) {\n  i++;\n}\n",
    "for_condition": "for (int i = 0; i + 1; i++) { print(i); }\n",
    "overflow": "int a = 2147483647;\na++;\n",
    "overflow_mul": "int a = 100000;\nprint(a * a);\n",
    "float_math": "float x = 1.5;\nprint(x * 2, x / 3, x - 5, -x, 7 % 3, 7.5 % 2);\nint z = 3;\nz += 2;\nz *= 4;\nprint(z);\n",
    "strings": "string s = \"a\" + 1 + 2.5 + true;\nprint(s, s == \"a12.5true\", \"b\" > \"a\", \"10\" - 3);\n",
    "compare_mixed": "print(1 == 1.0, 2 < 2.5, \"x\" != \"y\", true == false, [1, 2] == [1, 2], {\"a\": 1} != {\"a\": 2});\n",
    "short_circuit": "int z = 0;\nprint(z != 0 && 10 / z > 1, z == 0 || 1 / z > 0);\nprint(true && 5);\n",
    "not_bool": "print(!true);\nprint(!5);\n",
    "unary": "int a = 5;\nprint(-a, -(-a), -2147483648);\nprint(-\"x\");\n",
    "literal_huge": "print(1);\nint x = 99999999999999999999;\n",
    "literal_big": "print(3000000000);\nint y = 3000000000;\n",
    "postinc_expr": "int i = 5;\nint j = i++ + i;\nprint(i, j);\nint k = i + i++;\nprint(i, k);\nfloat f = 1.5;\nf++;\nprint(f);\nstring s = \"x\";\ns++;\n",
    "globals_and_locals": "int g = 1;\nvoid bump() { g = g + 1; g++; }\nbump();\nbump();\nprint(g);\nint read() { return hidden; }\nint owner() { int hidden = 3; return read(); }\nprint(owner());\n",
    "redeclare": "int a = 1;\nint a = 2;\n",
    "redeclare_first": "int a = 1;\nint a = missing;\n",
    "redeclare_local": "void f() {\n  int a = 1;\n  int a = 2;\n}\nf();\n",
    "global_keyword": "void f() { global int shared = 7; }\nf();\nprint(shared);\nf();\nprint(shared);\n",
    "block_scope": "int i = 0;\nwhile (i < 1) { int inner = 5; i++; }\nprint(inner);\n",
    "shadowing": "int x = 1;\nvoid f() { int x = 2; if (true) { int x = 3; print(x); } print(x); }\nf();\nprint(x);\n",
    "declared_types": "float f = 3;\nint n = to_int(3.9);\nstring s = 5;\nprint(f, n, s);\nf = 2;\nprint(f);\nbool b = 1;\n",
    "param_types": "float half(int v) { return v / 2; }\nprint(half(5), half(to_int(5.9)));\nvoid g(int x) {}\ng(\"abc\");\n",
    "return_types": "int f() { }\nint y = f();\n",
    "void_return": "void f() { return; }\nf();\nprint(\"ok\");\nint g() { return \"x\"; }\nprint(g());\n",
    "arguments": "void f(int x) {}\nf(1, 2);\n",
    "top_return": "print(\"before\");\nreturn 1;\nprint(\"after\");\n",
    "top_break": "print(\"a\");\nbreak;\n",
    "function_break": "void f() { print(\"in\"); break; }\nf();\n",
    "function_continue": "void f() { try { continue; } catch (e) { print(\"caught\", e); } finally { print(\"fin\"); } }\nf();\n",
    # Control flow
    "loops": (
        "int total = 0;\nfor (int i = 0; i < 10; i++) {\n  if (i == 2) { continue; }\n  if (i == 7) { break; }\n"
        "  total = total + i;\n}\nprint(total);\nint j = 0;\nwhile (true) { j++; if (j > 3) { break; } }\nprint(j);\n"
        "for (;;) { j--; if (j < 0) { break; } }\nprint(j);\nfor (int k = 0; k < 3; k++) { for (int m = 0; m < 3; m++) {"
        " if (m == 1) { continue; } if (k == 2) { break; } print(k, m); } }\n"
    ),
    "switch": (
        "void s(int v) {\n  switch (v) {\n    case 1: print(\"one\");\n    case 2: print(\"two\"); break;\n"
        "    case 3: { print(\"three\"); }\n    default: print(\"default\");\n  }\n}\ns(1); s(2); s(3); s(9);\n"
        "switch (\"a\") { case \"a\": print(\"text\"); }\nswitch (2) { case 2.0: print(\"number\"); }\n"
        "for (int i = 0; i < 3; i++) { switch (i) { case 1: continue; default: print(\"at\", i); } }\n"
        "int calls = 0;\nint next() { calls++; return calls; }\nswitch (1) { case next(): print(\"first\"); case next(): print(\"falls\"); }\nprint(calls);\n"
    ),
    "try_catch": (
        "try { print(\"body\"); fail(\"boom\"); print(\"never\"); } catch (e) { print(\"caught:\", e); }\n"
        "try { int x = 1 / 0; } catch (string e) { print(e); } finally { print(\"finally\"); }\n"
        "try { throw \"custom\"; } catch (e) { print(e); }\ntry { throw [1, 2]; } catch (e) { print(e); }\n"
        "try { print(\"ok\"); } finally { print(\"cleanup\"); }\n"
        "int f() { try { return 1; } finally { print(\"before return\"); } }\nprint(f());\n"
        "int g() { try { return 1; } finally { return 2; } }\nprint(g());\n"
        "int h() { int x = 1; try { return x; } finally { x = 5; } }\nprint(h());\n"
        "for (int i = 0; i < 3; i++) { try { if (i == 1) { continue; } if (i == 2) { break; } print(\"i\", i); } finally { print(\"f\", i); } }\n"
        "try { try { fail(\"inner\"); } finally { print(\"inner finally\"); } } catch (e) { print(\"outer caught\", e); }\n"
        "try { try { fail(\"a\"); } catch (e) { fail(\"b \" + e); } } catch (e) { print(e); }\n"
        "try { try { fail(\"a\"); } catch (e) { print(\"c1\"); } finally { fail(\"from finally\"); } } catch (e) { print(e); }\n"
        "void deep(int n) { if (n == 0) { fail(\"bottom\"); } try { deep(n - 1); } finally { print(\"unwind\", n); } }\n"
        "try { deep(3); } catch (e) { print(e); }\n"
        "int count = 0;\nwhile (count < 5) { try { count++; if (count == 2) { continue; } if (count == 4) { break; } } catch (e) { } finally { print(\"loop\", count); } }\n"
        "print(\"end\");\n"
    ),
    "finally_error_line": "void f() {\n  try {\n    print(\"x\");\n  } finally {\n    int z = 1 / 0;\n  }\n}\nf();\n",
    "uncaught_after_finally": "try {\n  fail(\"escapes\");\n} finally {\n  print(\"ran\");\n}\n",
    "exit_in_try": "try { print(\"a\"); exit(4); } catch (e) { print(\"no\"); } finally { print(\"finally on exit\"); }\n",
    "exit_in_function": "void f() { exit(0); }\nprint(\"x\");\nf();\nprint(\"y\");\n",
    "catch_scope": "try { fail(\"x\"); } catch (e) { print(e); }\nprint(e);\n",
    "error_in_catch": "try {\n  fail(\"one\");\n} catch (e) {\n  int bad = missing;\n}\n",
    # Containers
    "containers": (
        "array a = [1, 2, 3];\na[0] = 10;\na[1] += 5;\nprint(a, a[2], size(a));\nmap m = {\"x\": 1, y: [1, 2]};\n"
        "m[\"z\"] = 3;\nm.w = 4;\nm[\"y\"][0] = 9;\nm.x += 1;\nprint(m, m.y[1], m[\"x\"]);\n"
        "struct P { int x; float y = 1.5; string name = \"p\"; }\nP p = P(1);\np.x = 2;\np.y = 3;\nprint(p, p.y);\n"
        "array ps = [P(1), P(2)];\nps[1].x += 40;\nprint(ps[1].x);\nmap nested = {};\nnested[\"k\"] = {\"inner\": [1]};\n"
        "nested[\"k\"][\"inner\"][0] = 5;\nprint(nested);\narray b = a;\npush(b, 4);\nprint(a);\narray c = copy(a);\npush(c, 5);\nprint(a, c);\n"
        "map keys = {1: \"one\", \"2\": \"two\"};\nprint(keys[1], keys[\"1\"], keys[2]);\narray sized; resize(sized, 3);\nprint(sized);\narray empty;\nprint(empty);\n"
    ),
    "index_errors": "array a = [1];\nprint(a[5]);\n",
    "map_missing": "map m = {};\nprint(m.nothing);\n",
    "field_type": "struct P { int x; }\nP p = P(1);\np.x = \"text\";\n",
    "bad_key": "map m = {};\nm[1.5] = 2;\n",
    "bad_literal_key": "map m = {[1]: 2};\n",
    "array_size": "array a; resize(a, -1);\n",
    "struct_errors": "struct P { int x; }\nP p = P(1, 2);\n",
    "container_ops": "print([1] + 1);\n",
    "struct_cycle": "array loop = [1];\npush(loop, loop);\nprint(loop, loop == loop, size(copy(loop)[1]));\n",
    "functions_late": "int twice(int x) { return helper(x) * 2; }\nint helper(int x) { return x + 1; }\nprint(twice(3));\n",
    "redefine": "int f() { return 1; }\nprint(f());\nint f() { return 2; }\nprint(f());\nvoid g() { int f() { return 3; } }\ng();\nprint(f());\n",
    "recursion": "int fib(int n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }\nprint(fib(15));\n",
    "builtin_overlap": "array items = [1, 2];\nprint(get(items, 1));\nvoid get(string path, string handler) { print(\"route\", path, handler); }\nget(\"/\", \"home\");\nprint(get(items, 0));\n",
    "builtin_errors": "str_upper(5);\n",
    "builtin_count": "print(size());\n",
    "not_found": "print(1);\nnon_existent_func(1, 2);\n",
    "struct_as_function": "struct Pair { int a; int b = 2; }\nPair p = Pair(1);\nprint(p, Pair(3, 4));\nPair q;\nprint(q);\n",
    "zero_values": "int i;\n",
    "zero_values_ok": "map m;\nstruct S { int a; }\nS s;\nprint(m, s);\n",
    "module_using": "using string;\nusing math;\nprint(pad_left(\"7\", 3, \"0\"), PI > 3);\n",
    "throw_values": "throw 42;\n",
    "condition_after_body": "int k = 0;\nwhile (10 / (2 - k) > 0) {\n  k++;\n}\n",
    "loop_condition_later": "int limit(int k) {\n  if (k == 2) { fail(\"limit\"); }\n  return 5;\n}\nfor (int k = 0; k < limit(k); k++) {\n  print(k);\n}\n",
    "while_condition_later": "int n = 0;\nbool more() {\n  n++;\n  return n < 3;\n}\nwhile (more()) {\n  print(n);\n}\nwhile (n) {\n  print(\"x\");\n}\n",
    "if_chain": "void grade(int s) {\n  if (s > 90) { print(\"A\"); } else if (s > 80) { print(\"B\"); } else { print(\"C\"); }\n}\ngrade(95); grade(85); grade(10);\nif (\"x\") { print(1); }\n",
    "return_from_switch": (
        "string pick(int v) {\n  for (int i = 0; i < 10; i++) {\n    try {\n      switch (v) {\n        case 1: return \"one\";\n"
        "        case 2: if (i == 1) { return \"two at 1\"; } break;\n        default: continue;\n      }\n    } finally {\n"
        "      print(\"finally\", i);\n    }\n  }\n  return \"none\";\n}\nprint(pick(1));\nprint(pick(2));\nprint(pick(3));\n"
    ),
    "deferred_include": "void load() { include(\"tests/regression/helper_module.skm\"); }\nload();\nprint(\"loaded\");\n",
    "struct_default_error": "struct S {\n  int a = 1 / 0;\n}\nprint(\"before\");\nS s = S();\n",
    "struct_default_call": "int made = 0;\nint make() { made++; return made * 10; }\nstruct S { int a = make(); string t = \"x\"; }\nS one = S();\nS two = S(5);\nprint(one, two, made);\n",
    "nested_try_return": (
        "int f() {\n  try {\n    try {\n      return 1;\n    } finally {\n      print(\"inner\");\n    }\n  } finally {\n    print(\"outer\");\n  }\n}\nprint(f());\n"
        "int g(int n) {\n  while (true) {\n    try {\n      n++;\n      if (n > 2) { break; }\n    } finally {\n      print(\"g\", n);\n    }\n  }\n  return n;\n}\nprint(g(0));\n"
    ),
    "error_in_finally_after_return": "int f() {\n  try {\n    return 1;\n  } finally {\n    fail(\"in finally\");\n  }\n}\ntry { print(f()); } catch (e) { print(\"caught\", e); }\n",
    "catch_rethrow": "try {\n  try {\n    fail(\"x\");\n  } catch (e) {\n    throw \"again: \" + e;\n  }\n} catch (e) {\n  print(e);\n}\n",
    "compound_assign": "int a = 5;\na += 2;\na -= 1;\na *= 3;\na /= 2;\na %= 5;\nprint(a);\nstring s = \"x\";\ns += 1;\nprint(s);\nint big = 2147483647;\nbig += 1;\n",
    "compound_types": "int a = 5;\na += to_int(1.5);\nprint(a);\nfloat f = 1;\nf /= 4;\nprint(f);\nint b = 7;\nb /= 0;\n",
    "deep_recursion": "int down(int n) { return down(n + 1); }\ndown(0);\n",
    "recursion_caught": "int down(int n) { return down(n + 1); }\ntry { down(0); } catch (e) { print(\"stopped\"); }\nprint(\"alive\");\n",
    "strings_in_loops": "string s = \"\";\nfor (int i = 0; i < 5; i++) { s = s + i + \",\"; }\nprint(s);\narray parts = split(s, \",\");\nprint(size(parts), join(parts, \"-\"));\n",
    "map_iteration": "map m = {\"a\": 1, \"b\": 2};\narray k = keys(m);\nfor (int i = 0; i < size(k); i++) { print(k[i], m[k[i]]); }\nm[\"a\"]++;\n",
    "increment_errors": "array a = [1];\na++;\n",
    "global_increment": "int counter = 0;\nvoid tick() { counter++; counter--; counter++; }\ntick(); tick();\nint seen = counter++;\nprint(counter, seen);\n",
    "conditions_bool": "bool ok = true;\nif (ok) { print(\"yes\"); }\nwhile (!ok) { }\nfor (int i = 0; ok; i++) { ok = i < 2; print(i); }\n",
}

def run(binary, script, cwd):
    env = dict(os.environ)
    result = subprocess.run([binary, str(script)], capture_output=True, text=True, env=env, cwd=cwd,
                            timeout=120, input="")
    # How many calls fit on the native stack is not part of the language.
    normalize = lambda text: re.sub(r"after \d+ nested calls", "after N nested calls", text)
    return {"exit": result.returncode, "stdout": normalize(result.stdout), "stderr": normalize(result.stderr)}


def main():
    binary = Path(sys.argv[1]).resolve()
    update = "--update" in sys.argv
    expected = {} if update else json.loads(EXPECTED.read_text(encoding="utf-8"))
    actual = {}
    failures = []
    with tempfile.TemporaryDirectory() as work:
        for name, source in CASES.items():
            path = Path(work) / f"{name}.skm"
            path.write_text(source, encoding="utf-8")
            actual[name] = run(binary, path.name, work)
            if not update and actual[name] != expected.get(name):
                failures.append(name)
    if update:
        EXPECTED.write_text(json.dumps(actual, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")
        print(f"recorded {len(actual)} programs")
        return 0
    for name in failures:
        print(f"MISMATCH {name}\n  expected: {expected.get(name)!r}\n  actual:   {actual[name]!r}")
    print(f"{len(actual)} programs, {len(failures)} differ")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
