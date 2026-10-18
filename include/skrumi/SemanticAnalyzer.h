#pragma once
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <unordered_map>
#include "skrumi/AST.h"
#include "skrumi/SourceLocation.h"
#include "skrumi/SourceProvider.h"

namespace skrumi {

enum class SymbolKind {
    Variable,
    Function,
    Parameter,
    Builtin,
    Keyword,
    Module,
    Type            // a struct: its fields are in params
};

struct Symbol {
    std::string name;
    std::string type;              // e.g. "int", "string", "void"
    SymbolKind kind = SymbolKind::Variable;
    SourceRange declRange;         // Location where symbol was declared
    std::vector<FuncParam> params; // If function
    std::string returnType;        // If function
    std::string documentation;     // Description for hover/completion
    std::string fileUri;           // URI or path of declaration
    bool constant = false;         // a variable declared const, or an enum's name
    bool isEnum = false;           // a Type that is an enum: its values are in methods
    std::vector<Symbol> methods;   // If a struct: its methods, without the `this` parameter;
                                   // if a module alias (using math as m): the module's functions and variables
};

struct Scope {
    Scope* parent = nullptr;
    std::unordered_map<std::string, Symbol> symbols;

    Symbol* find(const std::string& name) {
        auto it = symbols.find(name);
        if (it != symbols.end()) return &it->second;
        if (parent) return parent->find(name);
        return nullptr;
    }

    Symbol* findCurrent(const std::string& name) {
        auto it = symbols.find(name);
        if (it != symbols.end()) return &it->second;
        return nullptr;
    }

    void getAllSymbols(std::vector<Symbol>& out) const {
        for (const auto& pair : symbols) {
            out.push_back(pair.second);
        }
        if (parent) parent->getAllSymbols(out);
    }
};

struct CompletionItem {
    std::string label;
    std::string kind;              // "Keyword", "Function", "Variable", "Type"
    std::string detail;            // e.g. "int counter" or "void print(...)"
    std::string documentation;
};

struct HoverInfo {
    std::string markdown;
    SourceRange range;
    bool found = false;
};

struct DefinitionInfo {
    SourceRange range;
    std::string fileUri;
    bool found = false;
};

// The declaration a name refers to, the same in every file of a program.
struct SymbolIdentity {
    bool found = false;
    std::string name;
    std::string file;              // canonical path of the declaring file
    SourceRange declaration;       // its name at the declaration
    SourceRange at;                // the occurrence that was asked about
    bool editable = false;         // declared in the program, not a builtin or std module
};

struct DocumentSymbolInfo {
    std::string name;
    std::string kind;              // "Function", "Variable", etc.
    SourceRange range;
    SourceRange selectionRange;
    std::vector<DocumentSymbolInfo> children;
};

struct ParameterInfo {
    std::string label;
    std::string documentation;
};

struct SignatureInfo {
    std::string label;
    std::string documentation;
    std::vector<ParameterInfo> parameters;
};

struct SignatureHelpResult {
    std::vector<SignatureInfo> signatures;
    int activeSignature = 0;
    int activeParameter = 0;
    bool found = false;
};

// Documentation of one std module, read from its source: `//!` lines describe the
// module, `///` lines above a function describe the function.
struct ModuleInfo {
    std::string name;
    std::string documentation;
};
const std::vector<ModuleInfo>& standardModules();

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(std::string currentFile = "", std::string skrumiHome = "",
                              std::shared_ptr<const SourceProvider> sources = nullptr);
    ~SemanticAnalyzer();

    // Declarations of the other files of the same program (see ProjectIndex), so a
    // function or global defined there is known here. Call before analyze().
    void addProjectFiles(const std::vector<std::string>& files);

    // Analyze the AST without executing user code
    void analyze(const BlockNode* root);

    const std::vector<Diagnostic>& getDiagnostics() const { return diagnostics; }

    // LSP query methods
    HoverInfo getHover(int line, int col, const std::string& code = "") const;
    DefinitionInfo getDefinition(int line, int col) const;
    std::vector<CompletionItem> getCompletions(int line, int col) const;
    // Fields and methods after `name.`, when the type of the variable `name` is a struct.
    std::vector<CompletionItem> getMemberCompletions(const std::string& name, int line, int col) const;
    std::vector<DocumentSymbolInfo> getDocumentSymbols() const;
    SignatureHelpResult getSignatureHelp(const std::string& code, int line, int col) const;
    const Symbol* findFunction(const std::string& name) const;
    SymbolIdentity symbolAt(int line, int col) const;
    // Every place in this file that names the declaration, the declaration included.
    std::vector<SourceRange> referencesTo(const SymbolIdentity& target) const;

private:
    std::string currentFile;
    std::string skrumiHome;
    std::shared_ptr<const SourceProvider> sources;
    std::set<std::string> loadedModules;
    std::vector<Diagnostic> diagnostics;

    std::unique_ptr<Scope> rootScope;
    Scope* currentScope = nullptr;

    struct SymbolRef {
        SourceRange range;
        Symbol symbol;
    };
    std::vector<SymbolRef> symbolRefs;
    std::vector<DocumentSymbolInfo> documentSymbols;

    std::string currentFuncReturnType;
    // Top-level variables of the file, visible inside function bodies wherever they are
    // declared: a function body runs only when called, after the file's globals exist.
    std::unordered_map<std::string, Symbol> fileGlobals;

    void visitNode(const Node* node);
    void visitBlock(const BlockNode* node);
    void declareFunction(const FuncDefNode* node);
    void visitFuncDef(const FuncDefNode* node);
    void visitFunctionBody(const FuncDefNode* node, SourceRange declRange);
    void visitMethodCall(const MethodCallNode* node);
    void visitField(const FieldNode* node);
    const Symbol* structOf(const Node* base);
    const Symbol* variableOf(const Node* base);
    void visitVarDecl(const VarDeclNode* node);
    void visitVarAssign(const VarAssignNode* node);
    void visitFuncCall(const FuncCallNode* node);
    void visitVarAccess(const VarAccessNode* node);
    void visitReturn(const ReturnNode* node);
    void visitIf(const IfNode* node);
    void visitWhile(const WhileNode* node);
    void visitFor(const ForNode* node);
    void visitForIn(const ForInNode* node);
    void visitLambda(const LambdaNode* node);
    void visitSwitch(const SwitchNode* node);
    void visitBinOp(const BinOpNode* node);
    void visitArrayDecl(const ArrayDeclNode* node);
    void visitUsing(const UsingNode* node);
    void visitInclude(const IncludeNode* node);
    void declareStruct(const StructDefNode* node, const std::string& uri, const std::string& documentation);
    void visitStructDef(const StructDefNode* node);
    void declareEnum(const EnumDefNode* node, const std::string& uri, const std::string& documentation);
    void visitEnumDef(const EnumDefNode* node);
    void visitTry(const TryNode* node);
    void checkType(const std::string& type, SourceRange range);
    std::string expressionType(const Node* node);
    void checkNarrowing(const std::string& type, const Node* value);
    void checkVariable(const std::string& name, SourceRange range);
    void checkWritable(const std::string& name, SourceRange range);
    void addSymbol(Scope* scope, const Symbol& symbol, SourceRange nameRange, bool warnOnRedeclaration);

    void enterScope();
    void exitScope();
    void addBuiltins();
    // Returns the module's identity, or "" when it cannot be read.
    std::string loadModuleSymbols(const ModuleImport& request, SourceRange importRange, bool quiet = false);
    std::unordered_map<std::string, std::vector<Symbol>> moduleMembers; // by identity
    Symbol aliasSymbol(const UsingNode* node, const std::string& identity);
};

} // namespace skrumi
