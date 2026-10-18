#!/usr/bin/env bash
set -euo pipefail

SKRUMI_BIN="${1:-./skrumi}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
TEMP_ERR_FOX="$WORK_DIR/malformed.skm"

run_expect_fail() {
    local code="$1"
    local pattern="$2"
    local desc="$3"

    echo "$code" > "$TEMP_ERR_FOX"
    local out
    set +e
    out=$("$SKRUMI_BIN" "$TEMP_ERR_FOX" 2>&1)
    local status=$?
    set -e

    if [[ "$status" -eq 0 ]]; then
        echo "FAIL: Expected failure for $desc, but exited 0"
        exit 1
    fi

    if ! echo "$out" | grep -q "$pattern"; then
        echo "FAIL: Output for $desc did not contain '$pattern'. Output was: $out"
        exit 1
    fi
}

# 1. Missing semicolon / syntax error
run_expect_fail "int a = 5" "Syntax Error" "missing semicolon"

# 2. Unexpected token
run_expect_fail "int = 5;" "Syntax Error" "missing identifier in var decl"

# 3. Undefined variable
run_expect_fail "int a = undefined_var + 1;" "Variable 'undefined_var' not found" "undefined variable access"

# 4. Undefined function
run_expect_fail "non_existent_func(1, 2);" "Function 'non_existent_func' not found" "undefined function call"

# 5. Division by zero
run_expect_fail "int z = 10 / 0;" "Division by zero" "integer division by zero"

# 6. Array bounds
run_expect_fail "array arr; resize(arr, 2); set(arr, 5, 10);" "Array index out of bounds" "array set out of bounds"
run_expect_fail "array arr; resize(arr, 2); int x = get(arr, -1);" "Array index out of bounds" "array get negative index"

# 7. Type mismatch assignment
run_expect_fail "int a = 5; a = \"text\";" "Type Error" "assigning string to int variable"

# 8. Numeric limits and hardening
run_expect_fail "int big = 3000000000;" "does not fit in int" "int literal out of range"
run_expect_fail "int a = 2000000000; int b = a + a;" "int overflow" "int addition overflow"
run_expect_fail "int forever(int n) { return forever(n + 1); } int r = forever(0);" "call depth limit" "runaway recursion"
run_expect_fail "if (1) { print(\"yes\"); }" "condition must be bool" "non-bool condition"
run_expect_fail "int reader() { return hidden; } int owner() { int hidden = 1; return reader(); } int x = owner();" "Variable 'hidden' not found" "callee cannot see caller locals"
run_expect_fail "int z = 10 / 0;" "malformed.skm:1: Runtime Error" "runtime error carries its file and line"
run_expect_fail "int a = 5" "malformed.skm:1: Syntax Error" "syntax error carries its file and line"

# 9. Block scope is a real scope
run_expect_fail "int i = 0; while (i < 1) { int inner = 5; i++; } print(inner);" "Variable 'inner' not found" "block variable outside its block"
run_expect_fail "for (int i = 0; i < 2; i++) { print(i); } print(i);" "Variable 'i' not found" "for variable outside its loop"
run_expect_fail "int a = 1; int a = 2;" "already declared in this scope" "redeclaration in the same scope"

# 10. Break outside loop in global scope
run_expect_fail "break;" "break" "break outside loop in global scope"

# 11. Calls are checked against the declared signatures
run_expect_fail "str_upper(5);" "argument 'text' of str_upper() must be string" "builtin argument type"
run_expect_fail "print(size());" "size() expects 1 arguments" "builtin argument count"
run_expect_fail "void f(int x) {} f(\"abc\");" "parameter 'x' of 'f'" "parameter type"
run_expect_fail "void f(int x) {} f(1, 2);" "expects 1 arguments, got 2" "parameter count"
run_expect_fail "int f() { } int y = f();" "ended without a value" "missing return value"
run_expect_fail "return 1;" "'return' outside of a function" "return at top level"
run_expect_fail "void f() { break; } f();" "'break' outside of loop in function 'f'" "break outside loop in a function"

# 12. Values that cannot be used
run_expect_fail "print(pop([]));" "pop() from an empty array" "pop from an empty array"
run_expect_fail "int n = to_int(\"12abc\");" "cannot convert '12abc'" "to_int of invalid text"
run_expect_fail "print([1] + 1);" "cannot be applied to an array" "arithmetic on an array"
run_expect_fail "bool b = !5;" "operand of '!' must be bool" "logical not of a number"
run_expect_fail "print(sqrt(-1));" "sqrt() of a negative number" "sqrt of a negative number"

# 13. Syntax checks
run_expect_fail "switch (1) { default: print(1); default: print(2); }" "Syntax Error" "two defaults"
run_expect_fail "int a = 5 # 3;" "Syntax Error: Unknown character" "unknown character"
run_expect_fail "void f(void x) {}" "expected a parameter type" "void parameter"

# 14. exit() ends the program with its code and prints nothing else
echo 'print("before"); exit(3); print("after");' > "$TEMP_ERR_FOX"
set +e
out=$("$SKRUMI_BIN" "$TEMP_ERR_FOX" 2>&1)
status=$?
set -e
if [[ "$status" -ne 3 || "$out" != "before" ]]; then
    echo "FAIL: exit(3) returned $status with output: $out"
    exit 1
fi

# 15. Non-existent file
set +e
out=$("$SKRUMI_BIN" "/path/to/definitely_not_existing_file_999.skm" 2>&1)
status=$?
set -e
if [[ "$status" -eq 0 ]]; then
    echo "FAIL: Expected non-zero status for missing file"
    exit 1
fi
if ! echo "$out" | grep -q "could not open file"; then
    echo "FAIL: Expected 'could not open file' in error output"
    exit 1
fi

echo "ERROR_HANDLING_OK"
exit 0
