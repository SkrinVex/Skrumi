#pragma once
#include <vector>
#include <memory>
#include <string>
#include "skrumi/Token.h"
#include "skrumi/AST.h"
#include "skrumi/SourceLocation.h"
#include "skrumi/SourceProvider.h"

namespace skrumi {

class Parser {
public:
    explicit Parser(std::vector<Token> t, std::string currentFile = "");

    std::unique_ptr<BlockNode> parseProgram();
    // Collects syntax errors instead of stopping at the first one, for editors.
    std::unique_ptr<BlockNode> parseProgramWithDiagnostics(std::vector<Diagnostic>& outDiagnostics);
    // A single expression and nothing after it, for a debugger's watch and console.
    std::unique_ptr<Node> parseExpression();

    const std::vector<Diagnostic>& getDiagnostics() const { return diagnostics; }
    const std::vector<ModuleImport>& getImports() const { return imports; }

    std::string currentFile;

private:
    std::vector<Token> tokens;
    size_t pos = 0;
    bool collecting = false;
    const std::string* file = nullptr;
    std::vector<Diagnostic> diagnostics;
    std::vector<ModuleImport> imports;

    std::unique_ptr<Node> statement();
    std::unique_ptr<BlockNode> parseBlock();
    std::unique_ptr<Node> simpleStatement();
    std::unique_ptr<Node> declaration(bool global);
    std::unique_ptr<Node> arrayDeclaration(bool global, SourcePosition start, const std::string& type);
    void checkContainerType(const std::string& type);
    std::unique_ptr<Node> functionDefinition(const std::string& returnType, const Token& name, SourcePosition start);
    std::unique_ptr<Node> forStatement();
    // After `for (`: a for-in header, `x in`, `int x in`, `k, v in`, rather than a for (;;).
    bool forInAhead() const;
    bool lambdaAhead() const;
    std::unique_ptr<Node> lambda(SourcePosition start);
    std::unique_ptr<Node> forInStatement(SourcePosition start);
    std::unique_ptr<Node> switchStatement();
    std::unique_ptr<Node> ifStatement();
    std::unique_ptr<Node> structDefinition();
    std::unique_ptr<Node> enumDefinition();
    std::unique_ptr<Node> tryStatement();
    // [index] and .field after a value, any number of times.
    std::unique_ptr<Node> postfix(std::unique_ptr<Node> node, SourcePosition start);
    std::unique_ptr<Node> mapLiteral();

    std::unique_ptr<Node> expression();
    std::unique_ptr<Node> conditional();
    std::unique_ptr<Node> bitOr();
    std::unique_ptr<Node> bitXor();
    std::unique_ptr<Node> bitAnd();
    std::unique_ptr<Node> equality();
    std::unique_ptr<Node> shift();
    std::unique_ptr<Node> logicalOr();
    std::unique_ptr<Node> logicalAnd();
    std::unique_ptr<Node> comparison();
    std::unique_ptr<Node> addition();
    std::unique_ptr<Node> multiplication();
    std::unique_ptr<Node> unary();
    std::unique_ptr<Node> primary();
    std::unique_ptr<Node> atom();
    std::vector<std::unique_ptr<Node>> arguments(TokenType close);

    Token consume(TokenType type);
    const Token& peek(size_t offset = 0) const;
    bool check(TokenType type) const { return peek().type == type; }
    bool match(TokenType type);
    bool isTypeKeyword(size_t offset = 0) const;
    // `Name name`: a variable, parameter or function of a struct type.
    bool isUserType(size_t offset = 0) const;
    bool isType(size_t offset = 0) const { return isTypeKeyword(offset) || isUserType(offset); }
    bool looksLikeFunction() const;
    SourcePosition previousEnd() const;
    [[noreturn]] void fail(const std::string& message) const;
    [[noreturn]] void error(const std::string& message) const;
    void synchronize();
};

} // namespace skrumi
