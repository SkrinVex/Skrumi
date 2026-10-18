#include "skrumi/Lexer.h"
#include "skrumi/Parser.h"
#include <iostream>
#include <cassert>
#include <stdexcept>

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

int main() {
    // 1. Precedence: 2 + 3 * 4 parses as 2 + (3 * 4)
    {
        skrumi::Lexer lexer("int res = 2 + 3 * 4;");
        skrumi::Parser parser(lexer.tokenize());
        auto program = parser.parseProgram();
        TEST_ASSERT(program->stmts.size() == 1);
        auto* decl = dynamic_cast<skrumi::VarDeclNode*>(program->stmts[0].get());
        TEST_ASSERT(decl != nullptr);
        TEST_ASSERT(decl->name == "res");
        auto* plusOp = dynamic_cast<skrumi::BinOpNode*>(decl->expr.get());
        TEST_ASSERT(plusOp != nullptr && plusOp->op == "+");
        auto* leftNum = dynamic_cast<skrumi::NumberNode*>(plusOp->left.get());
        TEST_ASSERT(leftNum != nullptr && leftNum->val == "2");
        auto* rightMul = dynamic_cast<skrumi::BinOpNode*>(plusOp->right.get());
        TEST_ASSERT(rightMul != nullptr && rightMul->op == "*");
    }

    // 2. Adjacent string literal concatenation in AST
    {
        skrumi::Lexer lexer("string s = \"Hello\" \", \" \"World!\";");
        skrumi::Parser parser(lexer.tokenize());
        auto program = parser.parseProgram();
        TEST_ASSERT(program->stmts.size() == 1);
        auto* decl = dynamic_cast<skrumi::VarDeclNode*>(program->stmts[0].get());
        TEST_ASSERT(decl != nullptr);
        auto* strNode = dynamic_cast<skrumi::StringNode*>(decl->expr.get());
        TEST_ASSERT(strNode != nullptr);
        TEST_ASSERT(strNode->val == "Hello, World!");
    }

    // 3. Unary minus and NOT
    {
        skrumi::Lexer lexer("int a = -10; bool b = !false;");
        skrumi::Parser parser(lexer.tokenize());
        auto program = parser.parseProgram();
        TEST_ASSERT(program->stmts.size() == 2);
    }

    // 4. Function definition AST
    {
        skrumi::Lexer lexer("int add(int a, int b) { return a + b; }");
        skrumi::Parser parser(lexer.tokenize());
        auto program = parser.parseProgram();
        TEST_ASSERT(program->stmts.size() == 1);
        auto* func = dynamic_cast<skrumi::FuncDefNode*>(program->stmts[0].get());
        TEST_ASSERT(func != nullptr);
        TEST_ASSERT(func->name == "add");
        TEST_ASSERT(func->returnType == "int");
        TEST_ASSERT(func->params.size() == 2);
        TEST_ASSERT(func->params[0].name == "a" && func->params[0].type == "int");
        TEST_ASSERT(func->params[1].name == "b" && func->params[1].type == "int");
    }

    // 5. Syntax error handling
    {
        bool threw = false;
        try {
            skrumi::Lexer lexer("int a = ;");
            skrumi::Parser parser(lexer.tokenize());
            parser.parseProgram();
        } catch (const std::runtime_error& e) {
            threw = true;
        }
        TEST_ASSERT(threw);
    }

    auto parse = [](const std::string& code) {
        skrumi::Lexer lexer(code);
        skrumi::Parser parser(lexer.tokenize());
        return parser.parseProgram();
    };

    // 6. Builtins parse as ordinary calls, anywhere an expression may stand
    {
        auto program = parse("print(\"a\", 1, x); string s = input(\"? \"); int n = size(\"abc\") + size(list);");
        TEST_ASSERT(program->stmts.size() == 3);
        auto* call = dynamic_cast<skrumi::FuncCallNode*>(program->stmts[0].get());
        TEST_ASSERT(call && call->name == "print" && call->args.size() == 3);
        auto* input = dynamic_cast<skrumi::VarDeclNode*>(program->stmts[1].get());
        TEST_ASSERT(input && dynamic_cast<skrumi::FuncCallNode*>(input->expr.get()));
    }

    // 7. Array forms: empty, literal, empty, indexing and element assignment
    {
        auto program = parse("array a; array b = [1, \"x\", [2]]; array c; a[0] = b[1]; a[i + 1] += 2;");
        TEST_ASSERT(program->stmts.size() == 5);
        auto* sized = dynamic_cast<skrumi::ArrayDeclNode*>(program->stmts[0].get());
        TEST_ASSERT(sized && !sized->initializer);
        auto* literal = dynamic_cast<skrumi::ArrayDeclNode*>(program->stmts[1].get());
        TEST_ASSERT(literal && literal->initializer);
        auto* elements = dynamic_cast<skrumi::ArrayLiteralNode*>(literal->initializer.get());
        TEST_ASSERT(elements && elements->elements.size() == 3);
        auto* empty = dynamic_cast<skrumi::ArrayDeclNode*>(program->stmts[2].get());
        TEST_ASSERT(empty && !empty->initializer);
        auto* store = dynamic_cast<skrumi::SetNode*>(program->stmts[3].get());
        TEST_ASSERT(store && store->op == "=" && dynamic_cast<skrumi::IndexNode*>(store->value.get()) &&
                    dynamic_cast<skrumi::IndexNode*>(store->target.get()));
        auto* compound = dynamic_cast<skrumi::SetNode*>(program->stmts[4].get());
        TEST_ASSERT(compound && compound->op == "+");
    }

    // 8. array as a parameter and result type; resize accepts a parenthesized expression
    {
        auto program = parse("array twice(array items, int n) { return items; } array sized; resize(sized, (2 + 1));");
        auto* fn = dynamic_cast<skrumi::FuncDefNode*>(program->stmts[0].get());
        TEST_ASSERT(fn && fn->returnType == "array" && fn->params[0].type == "array");
        TEST_ASSERT(dynamic_cast<skrumi::ArrayDeclNode*>(program->stmts[1].get()));
    }

    // 9. for with empty parts, compound conditions, i-- and %=
    {
        auto program = parse("for (;;) { break; } for (int i = 9; i > 0 && i != 5; i--) { n %= 2; }");
        auto* forever = dynamic_cast<skrumi::ForNode*>(program->stmts[0].get());
        TEST_ASSERT(forever && !forever->init && !forever->condition && !forever->step);
        auto* loop = dynamic_cast<skrumi::ForNode*>(program->stmts[1].get());
        TEST_ASSERT(loop && dynamic_cast<skrumi::BinOpNode*>(loop->condition.get()));
        auto* step = dynamic_cast<skrumi::PostIncNode*>(loop->step.get());
        TEST_ASSERT(step && step->delta == -1);
    }

    // 10. Negative literals are single numbers, so the smallest int is expressible
    {
        auto program = parse("int low = -2147483648; int neg = -x;");
        auto* low = dynamic_cast<skrumi::VarDeclNode*>(program->stmts[0].get());
        auto* number = dynamic_cast<skrumi::NumberNode*>(low->expr.get());
        TEST_ASSERT(number && number->val == "-2147483648");
        auto* neg = dynamic_cast<skrumi::VarDeclNode*>(program->stmts[1].get());
        TEST_ASSERT(dynamic_cast<skrumi::UnaryOpNode*>(neg->expr.get()));
    }

    // 11. global works for scalars and arrays
    {
        auto program = parse("void f() { global int total = 0; global array seen; resize(seen, 2); }");
        auto* fn = dynamic_cast<skrumi::FuncDefNode*>(program->stmts[0].get());
        auto* body = dynamic_cast<skrumi::BlockNode*>(fn->body.get());
        auto* total = dynamic_cast<skrumi::VarDeclNode*>(body->stmts[0].get());
        auto* seen = dynamic_cast<skrumi::ArrayDeclNode*>(body->stmts[1].get());
        TEST_ASSERT(total && total->global && seen && seen->global);
    }

    // 12. Syntax errors name what was expected and the line
    {
        std::string message;
        try {
            parse("int a = 1;\nswitch (a) { default: break; case 1: break; }");
        } catch (const std::runtime_error& e) {
            message = e.what();
        }
        TEST_ASSERT(message.find("Syntax Error") != std::string::npos && message.find("line 2") != std::string::npos);
        message.clear();
        try {
            parse("int a = 5");
        } catch (const std::runtime_error& e) {
            message = e.what();
        }
        TEST_ASSERT(message.find("expected ';'") != std::string::npos && message.find("line 1") != std::string::npos);
    }

    // 13. Error recovery keeps parsing after a broken statement
    {
        skrumi::Lexer lexer("int a = ;\nint b = 2;\nprint(b;\nint c = 3;");
        skrumi::Parser parser(lexer.tokenize());
        std::vector<skrumi::Diagnostic> diagnostics;
        auto program = parser.parseProgramWithDiagnostics(diagnostics);
        TEST_ASSERT(diagnostics.size() == 2);
        TEST_ASSERT(program->stmts.size() == 2);
    }

    std::cout << "TEST_PARSER_OK" << std::endl;
    return 0;
}
