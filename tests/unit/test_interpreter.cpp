#include "skrumi/Skrumi.h"
#include <iostream>
#include <cassert>

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

int main() {
    // CTest hides this output unless the test fails, and then it says which case a
    // crash stopped at on a platform this project cannot run locally.
    auto step = [](const char* what) { std::cout << "-> " << what << std::endl; };
    skrumi::Interpreter interpreter;

    // 1. Basic execution and variable retrieval
    {
        auto res = interpreter.runSource("int x = 40 + 2; string s = \"Skrumi\";");
        TEST_ASSERT(res.success);
        TEST_ASSERT(interpreter.getGlobal("x").text() == "42");
        TEST_ASSERT(interpreter.getGlobal("s").text() == "Skrumi");
    }

    // 2. Set global and use in script
    {
        interpreter.setGlobal("injected", "int", "100");
        auto res = interpreter.runSource("int total = injected + 50;");
        TEST_ASSERT(res.success);
        TEST_ASSERT(interpreter.getGlobal("total").text() == "150");
    }

    // 3. Reset clears state
    {
        interpreter.reset();
        bool threw = false;
        try {
            interpreter.getGlobal("x");
        } catch (const std::exception&) {
            threw = true;
        }
        TEST_ASSERT(threw);
    }

    // 4. Function call and recursion
    {
        const char* code = R"(
            int fact(int n) {
                if (n <= 1) { return 1; }
                return n * fact(n - 1);
            }
            int r = fact(5);
        )";
        auto res = interpreter.runSource(code);
        TEST_ASSERT(res.success);
        TEST_ASSERT(interpreter.getGlobal("r").text() == "120");
    }

    // 5. Division by zero cleanly caught
    {
        auto res = interpreter.runSource("int bad = 10 / 0;");
        TEST_ASSERT(!res.success);
        TEST_ASSERT(res.errorMessage.find("Division by zero") != std::string::npos);
    }

    // 6. Array bounds cleanly caught
    {
        const char* code = R"(
            array a; resize(a, 3);
            set(a, 10, 999);
        )";
        auto res = interpreter.runSource(code);
        TEST_ASSERT(!res.success);
        TEST_ASSERT(res.errorMessage.find("Array index out of bounds") != std::string::npos);
    }

    // 7. Isolation between multiple Interpreter instances in same process
    {
        skrumi::Interpreter interpA;
        skrumi::Interpreter interpB;

        auto resA = interpA.runSource("int varA = 111; int getVal() { return 1000; }");
        auto resB = interpB.runSource("int varB = 222; int getVal() { return 2000; }");

        TEST_ASSERT(resA.success && resB.success);
        TEST_ASSERT(interpA.getGlobal("varA").text() == "111");
        TEST_ASSERT(interpB.getGlobal("varB").text() == "222");

        // interpA should not see varB and interpB should not see varA
        bool aHasB = false;
        try { interpA.getGlobal("varB"); aHasB = true; } catch (...) {}
        TEST_ASSERT(!aHasB);

        bool bHasA = false;
        try { interpB.getGlobal("varA"); bHasA = true; } catch (...) {}
        TEST_ASSERT(!bHasA);

        auto callA = interpA.runSource("int checkA = getVal();");
        auto callB = interpB.runSource("int checkB = getVal();");
        TEST_ASSERT(callA.success && callB.success);
        TEST_ASSERT(interpA.getGlobal("checkA").text() == "1000");
        TEST_ASSERT(interpB.getGlobal("checkB").text() == "2000");
    }

    // 8. Repeated runSource() on the same Interpreter
    {
        skrumi::Interpreter interp;
        auto res1 = interp.runSource("int counter = 10; int addStep(int x) { return x + 5; }");
        TEST_ASSERT(res1.success);

        auto res2 = interp.runSource("counter = addStep(counter);");
        TEST_ASSERT(res2.success);
        TEST_ASSERT(interp.getGlobal("counter").text() == "15");

        auto res3 = interp.runSource("counter = addStep(counter);");
        TEST_ASSERT(res3.success);
        TEST_ASSERT(interp.getGlobal("counter").text() == "20");
    }

    // 9. Function defined inside nested block
    {
        skrumi::Interpreter interp;
        const char* code = R"(
            if (true) {
                int nestedFunc(int val) {
                    return val * 3;
                }
            }
            int result = nestedFunc(4);
        )";
        auto res = interp.runSource(code);
        TEST_ASSERT(res.success);
        TEST_ASSERT(interp.getGlobal("result").text() == "12");
    }

    // 10. Exception handling via public API
    {
        skrumi::Interpreter interp;
        // Syntax error
        auto resSyntax = interp.runSource("int x = ;");
        TEST_ASSERT(!resSyntax.success);
        TEST_ASSERT(resSyntax.exitCode != 0);
        TEST_ASSERT(!resSyntax.errorMessage.empty());

        // Type error
        auto resType = interp.runSource("bool b = 123;");
        TEST_ASSERT(!resType.success);
        TEST_ASSERT(resType.exitCode != 0);
        TEST_ASSERT(resType.errorMessage.find("Type Error") != std::string::npos);

        // Undefined variable
        auto resUndef = interp.runSource("int z = not_exists + 1;");
        TEST_ASSERT(!resUndef.success);
        TEST_ASSERT(resUndef.exitCode != 0);
        TEST_ASSERT(resUndef.errorMessage.find("Variable 'not_exists' not found") != std::string::npos);

        // Non-existent file
        auto resFile = interp.runFile("non_existent_file_12345.skm");
        TEST_ASSERT(!resFile.success);
        TEST_ASSERT(resFile.exitCode != 0);
        TEST_ASSERT(resFile.errorMessage.find("could not open file") != std::string::npos);
    }

    // 11. Float arithmetic keeps full double precision between operations
    {
        step("float precision");
        skrumi::Interpreter interp;
        auto res = interp.runSource("float third = 1.0 / 3.0; float back = third * 3.0; float sum = 0.1 + 0.2;");
        TEST_ASSERT(res.success);
        TEST_ASSERT(interp.getGlobal("third").text() == "0.3333333333333333");
        TEST_ASSERT(interp.getGlobal("back").text() == "1");
        TEST_ASSERT(interp.getGlobal("sum").text() == "0.30000000000000004");
    }

    // 12. int stays inside its range instead of wrapping or leaking std::stoi
    {
        step("int range");
        skrumi::Interpreter interp;
        auto tooBig = interp.runSource("int big = 3000000000;");
        TEST_ASSERT(!tooBig.success);
        TEST_ASSERT(tooBig.errorMessage.find("does not fit in int") != std::string::npos);

        auto overflow = interp.runSource("int a = 2000000000; int b = a + a;");
        TEST_ASSERT(!overflow.success);
        TEST_ASSERT(overflow.errorMessage.find("int overflow") != std::string::npos);
    }

    // 12b. A value is a kind and one word; strings and containers are shared, not copied
    {
        step("compact values");
        static_assert(sizeof(skrumi::Value) == 16, "a value is a kind and one machine word");
        skrumi::Value text = skrumi::Value::string("Лис");
        skrumi::Value copy = text;
        TEST_ASSERT(&copy.str() == &text.str());
        TEST_ASSERT(skrumi::Value::integer(7).text() == "7" && skrumi::Value::real(2.5).text() == "2.5");
        TEST_ASSERT(skrumi::Value::boolean(true).typeName() == "bool" && skrumi::Value().isVoid());

        skrumi::Interpreter interp;
        interp.setGlobal("ratio", "float", "2.5");
        interp.setGlobal("ready", "bool", "true");
        auto res = interp.runSource("float twice = ratio * 2; bool go = ready && true; int huge = 99999999999999999999;");
        TEST_ASSERT(!res.success && res.errorMessage.find("does not fit in int") != std::string::npos);
        TEST_ASSERT(interp.getGlobal("twice").isFloat() && interp.getGlobal("twice").asFloat() == 5.0);
        TEST_ASSERT(interp.getGlobal("go").asBool());
        bool rejected = false;
        try {
            interp.setGlobal("n", "int", "abc");
        } catch (const std::runtime_error& error) {
            rejected = std::string(error.what()).find("Type Error") != std::string::npos;
        }
        TEST_ASSERT(rejected);
    }

    // 13. Runtime errors name the line they happened on
    {
        step("error lines");
        skrumi::Interpreter interp;
        auto res = interp.runSource("int ok = 1;\nint bad = 10 / 0;");
        TEST_ASSERT(!res.success);
        TEST_ASSERT(res.errorMessage.rfind("<eval>:2: Runtime Error: Division by zero", 0) == 0);
    }

    // 14. Runaway recursion is an error, not a stack overflow
    {
        step("recursion guard");
        std::cout << "   stack budget: " << skrumi::platform::stackBudget() << " bytes" << std::endl;
        skrumi::Interpreter interp;
        auto res = interp.runSource("int forever(int n) { return forever(n + 1); } int r = forever(0);");
        TEST_ASSERT(!res.success);
        TEST_ASSERT(res.errorMessage.find("call depth limit") != std::string::npos);
    }

    // 15. Functions see globals, never the caller's locals
    {
        step("lexical scope");
        skrumi::Interpreter interp;
        const char* code = R"(
            global int shared = 7;
            int readShared() { return shared; }
            int caller() { int shared = 99; return readShared(); }
            int seen = caller();
        )";
        auto res = interp.runSource(code);
        TEST_ASSERT(res.success);
        TEST_ASSERT(interp.getGlobal("seen").text() == "7");

        auto leak = interp.runSource(
            "int reader() { return hidden; } int owner() { int hidden = 1; return reader(); } int x = owner();");
        TEST_ASSERT(!leak.success);
        TEST_ASSERT(leak.errorMessage.find("Variable 'hidden' not found") != std::string::npos);
    }

    // 16. Array buffers are released with their scope and on re-declaration
    {
        step("array lifetime");
        skrumi::Interpreter interp;
        auto res = interp.runSource(R"(
            void churn() { array tmp; resize(tmp, 16); set(tmp, 0, 1); }
            void loop() { int i = 0; while (i < 500) { array local; resize(local, 16); churn(); i++; } }
            loop();
        )");
        TEST_ASSERT(res.success);

        // Only the variable holds the array: nothing else keeps a copy alive.
        auto kept = interp.runSource("array survivor; resize(survivor, 4); set(survivor, 0, 5);");
        TEST_ASSERT(kept.success);
        TEST_ASSERT(interp.getContext().variables.at("survivor").ref()->refs == 1);
    }

    // 17. A block is a scope of its own, and the program top level is not
    {
        step("block scope");
        skrumi::Interpreter interp;
        auto leak = interp.runSource("int i = 0; while (i < 1) { int inner = 5; i++; } int seen = inner;");
        TEST_ASSERT(!leak.success);
        TEST_ASSERT(leak.errorMessage.find("Variable 'inner' not found") != std::string::npos);

        skrumi::Interpreter globals;
        auto top = globals.runSource("int kept = 3; { int hidden = 4; kept = kept + hidden; }");
        TEST_ASSERT(top.success);
        TEST_ASSERT(globals.getGlobal("kept").text() == "7");

        auto twice = globals.runSource("int kept = 1;");
        TEST_ASSERT(!twice.success);
        TEST_ASSERT(twice.errorMessage.find("already declared in this scope") != std::string::npos);
    }

    // 18. && and || stop before evaluating the right side
    {
        step("short circuit");
        skrumi::Interpreter interp;
        auto res = interp.runSource(
            "int zero = 0; bool guarded = zero != 0 && 10 / zero > 1; bool other = zero == 0 || 10 / zero > 1;");
        TEST_ASSERT(res.success);
        TEST_ASSERT(interp.getGlobal("guarded").text() == "false");
        TEST_ASSERT(interp.getGlobal("other").text() == "true");
    }

    // 19. A number carries its binary form but still prints the text it always did
    {
        step("number text");
        skrumi::Interpreter interp;
        auto res = interp.runSource(
            "int n = 21 * 2; string label = \"n = \" + n;"
            "float quarter = 1.0 / 4.0; string shown = \"\" + quarter;"
            "float tiny = 2.0 / 3.0; string precise = \"\" + tiny;"
            "bool same = n == 42;");
        TEST_ASSERT(res.success);
        TEST_ASSERT(interp.getGlobal("label").text() == "n = 42");
        TEST_ASSERT(interp.getGlobal("shown").text() == "0.25");
        TEST_ASSERT(interp.getGlobal("precise").text() == "0.6666666666666666");
        TEST_ASSERT(interp.getGlobal("same").text() == "true");
    }

    // 20. The recursion guard is only as good as the stack size it is told about
    {
        step("stack budget");
        size_t budget = skrumi::platform::stackBudget();
        if (budget < (256u << 10) || budget > (48u << 20)) {
            std::cerr << "FAIL: stack budget " << budget << " bytes is not believable" << std::endl;
            return 1;
        }
    }

    // 21. exit(code) ends the program with that code and no error message
    {
        step("exit");
        skrumi::Interpreter interp;
        auto res = interp.runSource("int before = 1; exit(4); int after = 2;");
        TEST_ASSERT(!res.success && res.exitCode == 4 && res.errorMessage.empty());
        TEST_ASSERT(interp.getGlobal("before").text() == "1");
        auto zero = interp.runSource("exit();");
        TEST_ASSERT(zero.success && zero.exitCode == 0);
    }

    // 22. Embedders pass command-line arguments through the options
    {
        step("arguments");
        skrumi::InterpreterOptions options;
        options.arguments = {"alpha", "бета"};
        skrumi::Interpreter interp(options);
        auto res = interp.runSource("array list = os_args(); int count = size(list); string second = list[1];");
        TEST_ASSERT(res.success);
        TEST_ASSERT(interp.getGlobal("count").text() == "2");
        TEST_ASSERT(interp.getGlobal("second").text() == "бета");
    }

    // 23. return outside a function is an error, not a crash
    {
        step("top-level return");
        skrumi::Interpreter interp;
        auto res = interp.runSource("return 1;");
        TEST_ASSERT(!res.success && res.errorMessage.find("outside of a function") != std::string::npos);
    }

    // 24. Temporary arrays die with the scope that made them
    {
        step("array ownership");
        skrumi::Interpreter interp;
        auto res = interp.runSource(
            "array make(int n) { array r; for (int i = 0; i < n; i++) { push(r, i); } return r; }"
            "for (int i = 0; i < 500; i++) { array parts = str_split(\"a,b\", \",\"); array made = make(3); int n = size(make(2)); }");
        TEST_ASSERT(res.success);
        auto kept = interp.runSource("array keep = make(3);");
        TEST_ASSERT(kept.success && interp.getContext().variables.at("keep").ref()->refs == 1 && interp.getContext().variables.at("keep").ref()->items.size() == 3);
    }

    // 25. Parameters and results convert to their declared types
    {
        step("typed calls");
        skrumi::Interpreter interp;
        auto res = interp.runSource(
            "float half(int v) { return v / 2; } string tag(string s) { return \"<\" + s + \">\"; }"
            "float h = half(7); string t = tag(5); string kind = type_of(half(7));");
        TEST_ASSERT(res.success);
        TEST_ASSERT(interp.getGlobal("h").text() == "3");
        TEST_ASSERT(interp.getGlobal("t").text() == "<5>");
        TEST_ASSERT(interp.getGlobal("kind").text() == "float");
        auto bad = interp.runSource("void want(int n) {} want(\"abc\");");
        TEST_ASSERT(!bad.success && bad.errorMessage.find("parameter 'n' of 'want'") != std::string::npos);
        auto missing = interp.runSource("int nothing() { } int x = nothing();");
        TEST_ASSERT(!missing.success && missing.errorMessage.find("ended without a value") != std::string::npos);
    }

    // 26. A builtin and a Skrumi function may share a name; the Skrumi declaration shadows by name
    {
        step("builtin shadowing");
        skrumi::Interpreter interp;
        auto res = interp.runSource(
            "string routed = \"\"; void get(string path, string handler) { routed = path + \"->\" + handler; }"
            "array items = [10, 20]; int second = items[1]; get(\"/health\", \"health\");");
        TEST_ASSERT(res.success);
        TEST_ASSERT(interp.getGlobal("second").text() == "20");
        TEST_ASSERT(interp.getGlobal("routed").text() == "/health->health");
    }

    // 27. Every builtin in the catalog can be found and describes itself
    {
        step("builtin catalog");
        const auto& catalog = skrumi::builtinCatalog();
        TEST_ASSERT(catalog.size() > 100);
        for (const auto& spec : catalog) {
            TEST_ASSERT(skrumi::runtime::isBuiltin(spec.name));
            TEST_ASSERT(!spec.documentation.empty());
            TEST_ASSERT(spec.required <= spec.params.size());
            TEST_ASSERT(spec.signature().rfind(spec.name + "(", 0) == 0);
        }
        TEST_ASSERT(skrumi::findBuiltinSpec("input")->signature() == "input([string prompt]) -> string");
        TEST_ASSERT(skrumi::findBuiltinSpec("print")->signature() == "print(any values...) -> void");
        for (const char* removed : {"httpget", "httppost", "fox", "readfile", "str_to_int", "route_get", "send_response"})
            TEST_ASSERT(!skrumi::runtime::isBuiltin(removed));
    }

    std::cout << "TEST_INTERPRETER_OK" << std::endl;
    return 0;
}
