#include "skrumi/SemanticAnalyzer.h"
#include "skrumi/Builtins.h"
#include "skrumi/Lexer.h"
#include "skrumi/Parser.h"
#include "skrumi/Project.h"
#include "EmbeddedStdlib.h"
#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace skrumi {

namespace {

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

std::string trim(const std::string& text) {
    auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

// Comment lines starting with marker, joined into one Markdown paragraph set.
std::string commentBlock(const std::vector<std::string>& source, size_t first, size_t last, const std::string& marker) {
    std::string out;
    for (size_t i = first; i < last; ++i) {
        std::string line = trim(source[i]);
        if (line.compare(0, marker.size(), marker) != 0) continue;
        line = line.substr(marker.size());
        if (!line.empty() && line[0] == ' ') line.erase(0, 1);
        if (!out.empty()) out += line.empty() ? "\n\n" : (out.back() == '\n' ? "" : "\n");
        out += line;
    }
    return out;
}

std::string moduleDescription(const std::string& text) {
    auto source = lines(text);
    size_t end = 0;
    while (end < source.size() && (trim(source[end]).rfind("//", 0) == 0 || trim(source[end]).empty())) ++end;
    return commentBlock(source, 0, end, "//!");
}

// `///` lines directly above the line where a function starts.
std::string functionComment(const std::vector<std::string>& source, int line) {
    if (line < 2) return "";
    size_t last = static_cast<size_t>(line - 1);
    size_t first = last;
    while (first > 0 && trim(source[first - 1]).rfind("///", 0) == 0) --first;
    return commentBlock(source, first, last, "///");
}

// A std function that only forwards to a builtin shares the builtin's documentation.
const BuiltinSpec* forwardedBuiltin(const FuncDefNode* fn) {
    auto* body = dynamic_cast<const BlockNode*>(fn->body.get());
    if (!body || body->stmts.size() != 1) return nullptr;
    const Node* stmt = body->stmts[0].get();
    if (auto* ret = dynamic_cast<const ReturnNode*>(stmt)) stmt = ret->expr.get();
    auto* call = dynamic_cast<const FuncCallNode*>(stmt);
    return call ? findBuiltinSpec(call->name) : nullptr;
}

std::string signatureOf(const std::string& name, const std::vector<FuncParam>& params, const std::string& result) {
    std::string out = name + "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) out += ", ";
        out += (params[i].type.empty() ? "" : params[i].type + " ") + params[i].name;
    }
    return out + ") -> " + result;
}

// A builtin shows its optional [parameters] and repeating ones...; a Skrumi function has neither.
std::string signatureOf(const Symbol& symbol) {
    if (symbol.kind == SymbolKind::Builtin) {
        if (const BuiltinSpec* spec = findBuiltinSpec(symbol.name)) return spec->signature();
    }
    return signatureOf(symbol.name, symbol.params, symbol.returnType);
}

} // namespace

const std::vector<ModuleInfo>& standardModules() {
    static const std::vector<ModuleInfo> modules = [] {
        std::vector<ModuleInfo> out;
        for (const auto& [path, text] : embeddedStdlib()) {
            std::string name = path.substr(4, path.size() - 8); // std/ + name + source suffix
            out.push_back({name, moduleDescription(text)});
        }
        return out;
    }();
    return modules;
}

SemanticAnalyzer::SemanticAnalyzer(std::string curFile, std::string home, std::shared_ptr<const SourceProvider> provider)
    : currentFile(std::move(curFile)), skrumiHome(std::move(home)), sources(std::move(provider)) {
    if (!sources) sources = filesystemSources(skrumiHome);
    rootScope = std::make_unique<Scope>();
    currentScope = rootScope.get();
    addBuiltins();
}

SemanticAnalyzer::~SemanticAnalyzer() {
    while (currentScope && currentScope != rootScope.get()) exitScope();
}

void SemanticAnalyzer::enterScope() {
    auto newScope = std::make_unique<Scope>();
    newScope->parent = currentScope;
    currentScope = newScope.release();
}

void SemanticAnalyzer::exitScope() {
    if (currentScope && currentScope->parent) {
        Scope* parent = currentScope->parent;
        delete currentScope;
        currentScope = parent;
    }
}

void SemanticAnalyzer::addBuiltins() {
    for (const auto& spec : builtinCatalog()) {
        Symbol sym;
        sym.name = spec.name;
        sym.type = spec.result;
        sym.kind = SymbolKind::Builtin;
        sym.returnType = spec.result;
        sym.params = spec.params;
        sym.documentation = spec.documentation;
        if (!spec.module.empty())
            sym.documentation += "\n\nФункция ядра; модуль `using " + spec.module + ";` даёт к ней обёртку с коротким именем.";
        rootScope->symbols[spec.name] = sym;
    }
}

void SemanticAnalyzer::addProjectFiles(const std::vector<std::string>& files) {
    // The file being edited is analyzed from its buffer, never from its copy on disk.
    loadedModules.insert(canonicalPath(currentFile));
    for (const auto& file : files) loadModuleSymbols({file, false}, {}, true);
}

std::string SemanticAnalyzer::loadModuleSymbols(const ModuleImport& request, SourceRange importRange, bool quiet) {
    std::string identity;
    std::string text;
    try {
        identity = sources->resolve(request, currentFile);
        text = sources->read(identity);
    } catch (const std::exception&) {
        if (!quiet) diagnostics.push_back({DiagnosticSeverity::Error, "Module '" + request.name + "' not found", importRange});
        return "";
    }
    if (!loadedModules.insert(identity).second) return identity;
    auto& members = moduleMembers[identity];

    Lexer lexer(text, true);
    Parser parser(lexer.tokenize(), identity);
    std::vector<Diagnostic> ignored;
    auto program = parser.parseProgramWithDiagnostics(ignored);
    auto source = lines(text);
    std::string uri = identity.rfind("@", 0) == 0 ? "" : identity;

    for (const auto& stmt : program->stmts) {
        if (auto* fn = dynamic_cast<const FuncDefNode*>(stmt.get())) {
            Symbol s;
            s.name = fn->name;
            s.type = fn->returnType;
            s.kind = SymbolKind::Function;
            s.returnType = fn->returnType;
            s.params = fn->params;
            s.declRange = fn->nameRange.start.line > 0 ? fn->nameRange : fn->range;
            s.fileUri = uri;
            s.documentation = functionComment(source, fn->range.start.line);
            if (s.documentation.empty()) {
                if (const BuiltinSpec* forwarded = forwardedBuiltin(fn)) s.documentation = forwarded->documentation;
            }
            if (s.documentation.empty()) s.documentation = signatureOf(fn->name, fn->params, fn->returnType);
            rootScope->symbols[fn->name] = s;
            members.push_back(s);
        } else if (auto* type = dynamic_cast<const StructDefNode*>(stmt.get())) {
            declareStruct(type, uri, functionComment(source, stmt->range.start.line));
        } else if (auto* enumeration = dynamic_cast<const EnumDefNode*>(stmt.get())) {
            declareEnum(enumeration, uri, functionComment(source, stmt->range.start.line));
        } else if (dynamic_cast<const VarDeclNode*>(stmt.get()) || dynamic_cast<const ArrayDeclNode*>(stmt.get())) {
            auto* var = dynamic_cast<const VarDeclNode*>(stmt.get());
            auto* arr = dynamic_cast<const ArrayDeclNode*>(stmt.get());
            Symbol s;
            s.name = var ? var->name : arr->name;
            s.type = var ? var->type : arr->type;
            s.kind = SymbolKind::Variable;
            s.constant = var ? var->constant : arr->constant;
            s.declRange = var ? var->nameRange : arr->nameRange;
            s.fileUri = uri;
            s.documentation = functionComment(source, stmt->range.start.line);
            if (s.documentation.empty()) s.documentation = s.type + " " + s.name;
            rootScope->symbols[s.name] = s;
            members.push_back(s);
        } else if (auto* use = dynamic_cast<const UsingNode*>(stmt.get())) {
            std::string saved = currentFile;
            currentFile = identity;
            loadModuleSymbols({use->libName, true}, importRange, quiet);
            currentFile = saved;
        } else if (auto* inc = dynamic_cast<const IncludeNode*>(stmt.get())) {
            std::string saved = currentFile;
            currentFile = identity;
            loadModuleSymbols({inc->filename, false}, importRange, quiet);
            currentFile = saved;
        }
    }
    return identity;
}

Symbol SemanticAnalyzer::aliasSymbol(const UsingNode* node, const std::string& identity) {
    Symbol alias;
    alias.name = node->alias;
    alias.type = "module " + node->libName;
    alias.kind = SymbolKind::Module;
    alias.declRange = node->aliasRange;
    alias.fileUri = currentFile;
    alias.documentation = "using " + node->libName + " as " + node->alias + ";";
    alias.constant = true;
    auto found = moduleMembers.find(identity);
    if (found != moduleMembers.end()) alias.methods = found->second;
    return alias;
}

void SemanticAnalyzer::analyze(const BlockNode* root) {
    diagnostics.clear();
    symbolRefs.clear();
    documentSymbols.clear();

    fileGlobals.clear();
    if (!root) return;
    // Functions may call functions and read globals defined further down the file.
    for (const auto& stmt : root->stmts) {
        if (auto* fn = dynamic_cast<const FuncDefNode*>(stmt.get())) {
            declareFunction(fn);
            continue;
        }
        if (auto* type = dynamic_cast<const StructDefNode*>(stmt.get())) {
            declareStruct(type, currentFile, "");
            continue;
        }
        if (auto* enumeration = dynamic_cast<const EnumDefNode*>(stmt.get())) {
            declareEnum(enumeration, currentFile, "");
            continue;
        }
        if (auto* use = dynamic_cast<const UsingNode*>(stmt.get())) {
            // An alias is a global name: function bodies above the line see it too.
            if (!use->alias.empty())
                fileGlobals.emplace(use->alias, aliasSymbol(use, loadModuleSymbols({use->libName, true}, use->range, true)));
            continue;
        }
        Symbol sym;
        sym.kind = SymbolKind::Variable;
        sym.fileUri = currentFile;
        if (auto* var = dynamic_cast<const VarDeclNode*>(stmt.get())) {
            sym.name = var->name;
            sym.type = var->type;
            sym.declRange = var->nameRange;
            sym.constant = var->constant;
        } else if (auto* arr = dynamic_cast<const ArrayDeclNode*>(stmt.get())) {
            sym.name = arr->name;
            sym.type = arr->type;
            sym.declRange = arr->nameRange;
            sym.constant = arr->constant;
        } else {
            continue;
        }
        sym.documentation = sym.type + " " + sym.name;
        fileGlobals.emplace(sym.name, sym);
    }
    visitBlock(root);
}

void SemanticAnalyzer::visitNode(const Node* node) {
    if (!node) return;

    if (auto* blk = dynamic_cast<const BlockNode*>(node)) {
        enterScope();
        visitBlock(blk);
        exitScope();
    } else if (auto* fn = dynamic_cast<const FuncDefNode*>(node)) {
        visitFuncDef(fn);
    } else if (auto* vd = dynamic_cast<const VarDeclNode*>(node)) {
        visitVarDecl(vd);
    } else if (auto* va = dynamic_cast<const VarAssignNode*>(node)) {
        visitVarAssign(va);
    } else if (auto* fc = dynamic_cast<const FuncCallNode*>(node)) {
        visitFuncCall(fc);
    } else if (auto* acc = dynamic_cast<const VarAccessNode*>(node)) {
        visitVarAccess(acc);
    } else if (auto* ret = dynamic_cast<const ReturnNode*>(node)) {
        visitReturn(ret);
    } else if (auto* ifn = dynamic_cast<const IfNode*>(node)) {
        visitIf(ifn);
    } else if (auto* wh = dynamic_cast<const WhileNode*>(node)) {
        visitWhile(wh);
    } else if (auto* fr = dynamic_cast<const ForNode*>(node)) {
        visitFor(fr);
    } else if (auto* each = dynamic_cast<const ForInNode*>(node)) {
        visitForIn(each);
    } else if (auto* sw = dynamic_cast<const SwitchNode*>(node)) {
        visitSwitch(sw);
    } else if (auto* bop = dynamic_cast<const BinOpNode*>(node)) {
        visitBinOp(bop);
    } else if (auto* un = dynamic_cast<const UnaryOpNode*>(node)) {
        visitNode(un->operand.get());
    } else if (auto* inc = dynamic_cast<const PostIncNode*>(node)) {
        checkVariable(inc->name, inc->range);
        checkWritable(inc->name, inc->range);
    } else if (auto* arr = dynamic_cast<const ArrayDeclNode*>(node)) {
        visitArrayDecl(arr);
    } else if (auto* lambda = dynamic_cast<const LambdaNode*>(node)) {
        visitLambda(lambda);
    } else if (auto* text = dynamic_cast<const InterpolationNode*>(node)) {
        for (const auto& part : text->parts) visitNode(part.get());
    } else if (auto* lit = dynamic_cast<const ArrayLiteralNode*>(node)) {
        for (const auto& element : lit->elements) visitNode(element.get());
    } else if (auto* index = dynamic_cast<const IndexNode*>(node)) {
        visitNode(index->base.get());
        visitNode(index->index.get());
    } else if (auto* field = dynamic_cast<const FieldNode*>(node)) {
        visitField(field);
    } else if (auto* method = dynamic_cast<const MethodCallNode*>(node)) {
        visitMethodCall(method);
    } else if (auto* call = dynamic_cast<const CallNode*>(node)) {
        visitNode(call->callee.get());
        for (const auto& arg : call->args) visitNode(arg.get());
    } else if (auto* set = dynamic_cast<const SetNode*>(node)) {
        visitNode(set->value.get());
        visitNode(set->target.get());
        checkNarrowing(expressionType(set->target.get()), set->value.get());
    } else if (auto* map = dynamic_cast<const MapLiteralNode*>(node)) {
        for (const auto& entry : map->entries) {
            visitNode(entry.first.get());
            visitNode(entry.second.get());
        }
    } else if (auto* type = dynamic_cast<const StructDefNode*>(node)) {
        visitStructDef(type);
    } else if (auto* enumeration = dynamic_cast<const EnumDefNode*>(node)) {
        visitEnumDef(enumeration);
    } else if (auto* guarded = dynamic_cast<const TryNode*>(node)) {
        visitTry(guarded);
    } else if (auto* raise = dynamic_cast<const ThrowNode*>(node)) {
        visitNode(raise->message.get());
    } else if (auto* usg = dynamic_cast<const UsingNode*>(node)) {
        visitUsing(usg);
    } else if (auto* incl = dynamic_cast<const IncludeNode*>(node)) {
        visitInclude(incl);
    }
}

void SemanticAnalyzer::visitBlock(const BlockNode* node) {
    for (const auto& stmt : node->stmts) {
        visitNode(stmt.get());
    }
}

void SemanticAnalyzer::declareFunction(const FuncDefNode* node) {
    Symbol fnSym;
    fnSym.name = node->name;
    fnSym.type = node->returnType;
    fnSym.kind = SymbolKind::Function;
    fnSym.returnType = node->returnType;
    fnSym.params = node->params;
    fnSym.declRange = node->nameRange.start.line > 0 ? node->nameRange : node->range;
    fnSym.fileUri = currentFile;
    std::string sig = node->returnType + " " + node->name + "(";
    for (size_t i = 0; i < node->params.size(); i++) {
        if (i > 0) sig += ", ";
        sig += (node->params[i].type.empty() ? "" : node->params[i].type + " ") + node->params[i].name;
    }
    fnSym.documentation = sig + ")";
    rootScope->symbols[node->name] = fnSym;
}

void SemanticAnalyzer::visitFuncDef(const FuncDefNode* node) {
    declareFunction(node);
    const Symbol& fnSym = rootScope->symbols[node->name];
    symbolRefs.push_back({fnSym.declRange, fnSym});

    DocumentSymbolInfo docSym;
    docSym.name = node->name;
    docSym.kind = "Function";
    docSym.range = node->range;
    docSym.selectionRange = fnSym.declRange;
    documentSymbols.push_back(docSym);
    visitFunctionBody(node, fnSym.declRange);
}

void SemanticAnalyzer::visitFunctionBody(const FuncDefNode* node, SourceRange declRange) {
    Symbol fnSym;
    fnSym.declRange = declRange;
    enterScope();
    std::string oldReturn = currentFuncReturnType;
    currentFuncReturnType = node->returnType;

    checkType(node->returnType, fnSym.declRange);
    for (const auto& param : node->params) {
        bool named = param.range.start.line != param.range.end.line || param.range.start.column != param.range.end.column;
        if (!param.type.empty()) checkType(param.type, named ? param.range : fnSym.declRange);
        Symbol paramSym;
        paramSym.name = param.name;
        paramSym.type = param.type.empty() ? "any" : param.type;
        paramSym.kind = SymbolKind::Parameter;
        paramSym.documentation = "parameter " + (param.type.empty() ? "" : param.type + " ") + param.name;
        paramSym.fileUri = currentFile;
        paramSym.declRange = named ? param.range : fnSym.declRange;
        currentScope->symbols[param.name] = paramSym;
        if (named) symbolRefs.push_back({param.range, paramSym});
    }

    if (node->body) visitNode(node->body.get());

    currentFuncReturnType = oldReturn;
    exitScope();
}

void SemanticAnalyzer::addSymbol(Scope* scope, const Symbol& sym, SourceRange nameRange, bool warnOnRedeclaration) {
    if (warnOnRedeclaration && scope->findCurrent(sym.name) && scope->findCurrent(sym.name)->kind != SymbolKind::Builtin) {
        diagnostics.push_back({DiagnosticSeverity::Warning,
            "Redeclaration of variable '" + sym.name + "' in the same scope", nameRange});
    }
    scope->symbols[sym.name] = sym;
    symbolRefs.push_back({nameRange, sym});
    if (scope == rootScope.get()) {
        DocumentSymbolInfo docSym;
        docSym.name = sym.name;
        docSym.kind = "Variable";
        docSym.range = nameRange;
        docSym.selectionRange = nameRange;
        documentSymbols.push_back(docSym);
    }
}

void SemanticAnalyzer::declareStruct(const StructDefNode* node, const std::string& uri, const std::string& documentation) {
    Symbol sym;
    sym.name = node->type->name;
    sym.type = node->type->name;
    sym.kind = SymbolKind::Type;
    sym.params = node->type->fields;
    sym.returnType = node->type->name;
    sym.declRange = node->nameRange.start.line > 0 ? node->nameRange : node->range;
    sym.fileUri = uri;
    std::string shape = "struct " + sym.name + " {";
    for (const auto& field : node->type->fields) shape += " " + field.type + " " + field.name + ";";
    for (const auto& method : node->methods) {
        Symbol m;
        m.name = method->name.substr(method->name.find('.') + 1);
        m.type = method->returnType;
        m.kind = SymbolKind::Function;
        m.returnType = method->returnType;
        m.params.assign(method->params.begin() + 1, method->params.end());
        m.declRange = method->nameRange.start.line > 0 ? method->nameRange : method->range;
        m.fileUri = uri;
        std::string sig = method->returnType + " " + method->name + "(";
        for (size_t i = 0; i < m.params.size(); ++i)
            sig += (i ? ", " : "") + (m.params[i].type.empty() ? "" : m.params[i].type + " ") + m.params[i].name;
        m.documentation = sig + ")";
        shape += " " + method->returnType + " " + m.name + "(...);";
        sym.methods.push_back(std::move(m));
    }
    sym.documentation = (documentation.empty() ? "" : documentation + "\n\n") + shape + " }";
    rootScope->symbols[sym.name] = sym;
}

void SemanticAnalyzer::declareEnum(const EnumDefNode* node, const std::string& uri, const std::string& documentation) {
    Symbol sym;
    sym.name = node->type->name;
    sym.type = node->type->name;
    sym.kind = SymbolKind::Type;
    sym.isEnum = true;
    sym.constant = true;
    sym.params = node->type->fields;
    sym.returnType = node->type->name;
    sym.declRange = node->nameRange.start.line > 0 ? node->nameRange : node->range;
    sym.fileUri = uri;
    std::string shape = "enum " + sym.name + " {";
    for (size_t i = 0; i < node->members.size(); ++i) {
        const auto& member = node->members[i];
        Symbol value;
        value.name = member.name;
        value.type = sym.name;
        value.kind = SymbolKind::Variable;
        value.constant = true;
        value.declRange = member.range;
        value.fileUri = uri;
        std::string shown = member.value.isString() ? "\"" + member.value.str() + "\"" : member.value.text();
        value.documentation = sym.name + "." + member.name + " = " + shown;
        shape += std::string(i ? ", " : " ") + member.name;
        sym.methods.push_back(std::move(value));
    }
    sym.documentation = (documentation.empty() ? "" : documentation + "\n\n") + shape + " }";
    rootScope->symbols[sym.name] = sym;
}

void SemanticAnalyzer::visitEnumDef(const EnumDefNode* node) {
    declareEnum(node, currentFile, "");
    const Symbol& sym = rootScope->symbols[node->type->name];
    symbolRefs.push_back({sym.declRange, sym});
    documentSymbols.push_back({node->type->name, "Enum", node->range, sym.declRange, {}});
    for (const auto& value : sym.methods) symbolRefs.push_back({value.declRange, value});
}

void SemanticAnalyzer::visitStructDef(const StructDefNode* node) {
    declareStruct(node, currentFile, "");
    const Symbol& sym = rootScope->symbols[node->type->name];
    symbolRefs.push_back({sym.declRange, sym});
    documentSymbols.push_back({node->type->name, "Struct", node->range, sym.declRange, {}});
    for (size_t i = 0; i < node->type->fields.size(); ++i) {
        SourceRange where = i < node->fieldRanges.size() ? node->fieldRanges[i] : node->range;
        checkType(node->type->fields[i].type, where);
        if (i < node->type->defaults.size()) { visitNode(node->type->defaults[i].get()); checkNarrowing(node->type->fields[i].type, node->type->defaults[i].get()); }
    }
    for (size_t i = 0; i < node->methods.size() && i < sym.methods.size(); ++i) {
        const Symbol& method = sym.methods[i];
        symbolRefs.push_back({method.declRange, method});
        documentSymbols.push_back({node->type->name + "." + method.name, "Method", node->methods[i]->range, method.declRange, {}});
        visitFunctionBody(node->methods[i].get(), method.declRange);
    }
}

// What a plain name before `.` refers to.
const Symbol* SemanticAnalyzer::variableOf(const Node* base) {
    auto* access = dynamic_cast<const VarAccessNode*>(base);
    if (!access) return nullptr;
    Symbol* variable = currentScope->find(access->name);
    if ((!variable || variable->kind == SymbolKind::Builtin || variable->kind == SymbolKind::Function) &&
        !currentFuncReturnType.empty()) {
        auto global = fileGlobals.find(access->name);
        if (global != fileGlobals.end()) variable = &global->second;
    }
    return variable;
}

// The struct that a variable (or `this`) holds, when its declared type says so.
const Symbol* SemanticAnalyzer::structOf(const Node* base) {
    const Symbol* variable = variableOf(base);
    if (!variable || (variable->kind != SymbolKind::Variable && variable->kind != SymbolKind::Parameter)) return nullptr;
    const std::string& declared = variable->type;
    Symbol* type = rootScope->find(isNullable(declared) ? declared.substr(0, declared.size() - 1) : declared);
    return type && type->kind == SymbolKind::Type ? type : nullptr;
}

void SemanticAnalyzer::visitMethodCall(const MethodCallNode* node) {
    visitNode(node->base.get());
    for (const auto& arg : node->args) visitNode(arg.get());
    const Symbol* named = variableOf(node->base.get());
    if (named && named->kind == SymbolKind::Module) {
        for (const auto& member : named->methods) {
            if (member.name != node->name) continue;
            symbolRefs.push_back({node->nameRange, member});
            for (size_t i = 0; i < node->args.size() && i < member.params.size(); ++i) checkNarrowing(member.params[i].type, node->args[i].get());
            if (member.kind == SymbolKind::Function && member.params.size() != node->args.size())
                diagnostics.push_back({DiagnosticSeverity::Error,
                    "Function '" + named->name + "." + node->name + "' expects " + std::to_string(member.params.size()) +
                    " arguments, but got " + std::to_string(node->args.size()), node->range});
            else if (member.kind == SymbolKind::Variable && member.type != "func" && member.type != "func?")
                diagnostics.push_back({DiagnosticSeverity::Error,
                    "'" + named->name + "." + node->name + "' is a " + member.type + " variable, not a function", node->nameRange});
            return;
        }
        if (const BuiltinSpec* builtin = findBuiltinSpec(node->name)) {
            // A builtin works through the alias too: m.sqrt(2).
            symbolRefs.push_back({node->nameRange, rootScope->symbols[node->name]});
            if (!builtin->acceptsCount(node->args.size()))
                diagnostics.push_back({DiagnosticSeverity::Error,
                    "Function '" + node->name + "' expects " + std::to_string(builtin->required) + " arguments, but got " +
                    std::to_string(node->args.size()), node->range});
            return;
        }
        diagnostics.push_back({DiagnosticSeverity::Error,
            "Module '" + named->type.substr(7) + "' has no function '" + node->name + "'", node->nameRange});
        return;
    }
    const Symbol* type = structOf(node->base.get());
    if (!type) return;
    for (const auto& method : type->methods) {
        if (method.name != node->name) continue;
        symbolRefs.push_back({node->nameRange, method});
        if (method.params.size() != node->args.size())
            diagnostics.push_back({DiagnosticSeverity::Error,
                "Method '" + type->name + "." + node->name + "' expects " + std::to_string(method.params.size()) +
                " arguments, but got " + std::to_string(node->args.size()), node->range});
        return;
    }
    for (const auto& field : type->params) {
        if (field.name != node->name) continue;
        if (field.type != "func" && field.type != "func?")
            diagnostics.push_back({DiagnosticSeverity::Error,
                "Field '" + type->name + "." + node->name + "' is " + field.type + ", not a function", node->nameRange});
        return;
    }
    diagnostics.push_back({DiagnosticSeverity::Error, "Struct '" + type->name + "' has no method '" + node->name + "'", node->nameRange});
}

void SemanticAnalyzer::visitField(const FieldNode* node) {
    visitNode(node->base.get());
    const Symbol* named = variableOf(node->base.get());
    if (named && named->kind == SymbolKind::Type && named->isEnum) {
        for (const auto& value : named->methods) {
            if (value.name != node->name) continue;
            symbolRefs.push_back({node->nameRange, value});
            return;
        }
        diagnostics.push_back({DiagnosticSeverity::Error, "Enum '" + named->name + "' has no value '" + node->name + "'", node->nameRange});
        return;
    }
    if (named && named->kind == SymbolKind::Module) {
        for (const auto& member : named->methods) {
            if (member.name != node->name) continue;
            symbolRefs.push_back({node->nameRange, member});
            return;
        }
        diagnostics.push_back({DiagnosticSeverity::Error,
            "Module '" + named->type.substr(7) + "' has no '" + node->name + "'", node->nameRange});
        return;
    }
    const Symbol* type = structOf(node->base.get());
    if (!type) return;
    for (const auto& field : type->params)
        if (field.name == node->name) return;
    diagnostics.push_back({DiagnosticSeverity::Error, "Struct '" + type->name + "' has no field '" + node->name + "'", node->nameRange});
}

void SemanticAnalyzer::visitTry(const TryNode* node) {
    visitNode(node->body.get());
    if (node->handler) {
        enterScope();
        if (!node->errorName.empty()) {
            Symbol error;
            error.name = node->errorName;
            error.type = "string";
            error.kind = SymbolKind::Variable;
            error.declRange = node->errorRange;
            error.documentation = "string " + node->errorName + " — сообщение об ошибке";
            error.fileUri = currentFile;
            addSymbol(currentScope, error, node->errorRange, false);
        }
        visitNode(node->handler.get());
        exitScope();
    }
    visitNode(node->cleanup.get());
}

void SemanticAnalyzer::checkType(const std::string& written, SourceRange range) {
    if (written == "void?") {
        diagnostics.push_back({DiagnosticSeverity::Error, "Type 'void' cannot be nullable", range});
        return;
    }
    const std::string type = isNullable(written) ? written.substr(0, written.size() - 1) : written;
    if (!type.empty() && type.back() == '>') {
        Value::Kind kind;
        std::string element;
        if (!runtime::containerType(type, kind, element)) {
            diagnostics.push_back({DiagnosticSeverity::Error, "Unknown type '" + type + "': map keys are text, write map<string, T>", range});
            return;
        }
        checkType(element, range);
        return;
    }
    static const std::set<std::string> builtinTypes = {"int", "float", "string", "bool", "void", "array", "map", "func"};
    if (builtinTypes.count(type)) return;
    Symbol* sym = rootScope->find(type);
    if (!sym || sym->kind != SymbolKind::Type)
        diagnostics.push_back({DiagnosticSeverity::Error, "Unknown type '" + type + "'", range});
}

// Conservative type facts: unknown dynamic values are checked by the runtime.
std::string SemanticAnalyzer::expressionType(const Node* node) {
    if (!node) return "";
    if (auto* n = dynamic_cast<const NumberNode*>(node)) return n->isFloat ? "float" : "int";
    if (auto* v = dynamic_cast<const VarAccessNode*>(node)) {
        const Symbol* symbol = variableOf(v);
        return symbol ? symbol->type : "";
    }
    if (auto* u = dynamic_cast<const UnaryOpNode*>(node)) return expressionType(u->operand.get());
    if (auto* b = dynamic_cast<const BinOpNode*>(node)) {
        if (b->kind != runtime::Operator::Add && b->kind != runtime::Operator::Sub && b->kind != runtime::Operator::Mul && b->kind != runtime::Operator::Div && b->kind != runtime::Operator::Mod) return "";
        auto left = expressionType(b->left.get()), right = expressionType(b->right.get());
        if ((left == "int" || left == "float") && (right == "int" || right == "float"))
            return left == "float" || right == "float" ? "float" : "int";
    }
    if (auto* f = dynamic_cast<const FuncCallNode*>(node)) {
        const Symbol* symbol = currentScope->find(f->name);
        if ((!symbol || symbol->kind == SymbolKind::Builtin) &&
            (f->name == "min" || f->name == "max" || f->name == "clamp" || f->name == "abs")) {
            bool allInt = !f->args.empty();
            for (const auto& arg : f->args) {
                auto type = expressionType(arg.get());
                if (type == "float") return "float";
                allInt = allInt && type == "int";
            }
            return allInt ? "int" : "";
        }
        if (symbol && (symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::Builtin)) return symbol->returnType;
        if (!symbol) if (const auto* builtin = findBuiltinSpec(f->name)) return builtin->result;
    }
    if (auto* index = dynamic_cast<const IndexNode*>(node)) {
        Value::Kind kind; std::string element;
        if (runtime::containerType(expressionType(index->base.get()), kind, element)) return element;
    }
    if (auto* field = dynamic_cast<const FieldNode*>(node)) {
        const Symbol* type = structOf(field->base.get());
        if (type) for (const auto& param : type->params) if (param.name == field->name) return param.type;
        Value::Kind kind; std::string element;
        if (runtime::containerType(expressionType(field->base.get()), kind, element)) return element;
    }
    if (auto* method = dynamic_cast<const MethodCallNode*>(node)) {
        const Symbol* owner = variableOf(method->base.get());
        if (!owner || owner->kind != SymbolKind::Module) owner = structOf(method->base.get());
        if (owner) for (const auto& member : owner->methods)
            if (member.name == method->name) return member.returnType;
    }
    return "";
}

void SemanticAnalyzer::checkNarrowing(const std::string& declared, const Node* value) {
    if (!value) return;
    std::string type = isNullable(declared) ? declared.substr(0, declared.size() - 1) : declared;
    if (type == "int" && expressionType(value) == "float") {
        SourceRange where = value->range;
        // Compound assignment synthesizes a binary expression without a range.
        if (where.start.line == 0)
            if (auto* binary = dynamic_cast<const BinOpNode*>(value)) where = binary->right->range;
        diagnostics.push_back({DiagnosticSeverity::Error, "Cannot implicitly convert float to int; use to_int()", where});
    }
    Value::Kind kind; std::string element;
    if (!runtime::containerType(type, kind, element)) return;
    if (auto* array = dynamic_cast<const ArrayLiteralNode*>(value))
        for (const auto& item : array->elements) checkNarrowing(element, item.get());
    if (auto* map = dynamic_cast<const MapLiteralNode*>(value))
        for (const auto& item : map->entries) checkNarrowing(element, item.second.get());
}

void SemanticAnalyzer::visitVarDecl(const VarDeclNode* node) {
    // func f = (n) => ... f(n - 1): a local lambda may call itself by its variable.
    bool recursive = !node->global && currentScope != rootScope.get() && node->type == "func" &&
                     dynamic_cast<const LambdaNode*>(node->expr.get());
    if (node->expr && !recursive) visitNode(node->expr.get());
    checkType(node->type, node->range);
    checkNarrowing(node->type, node->expr.get());
    if (dynamic_cast<const NullNode*>(node->expr.get()) && !isNullable(node->type))
        diagnostics.push_back({DiagnosticSeverity::Error,
            "Variable '" + node->name + "' of type " + node->type + " cannot be null; declare it " + node->type + "?",
            node->nameRange.start.line > 0 ? node->nameRange : node->range});
    Symbol sym;
    sym.name = node->name;
    sym.type = node->type;
    sym.kind = SymbolKind::Variable;
    sym.declRange = node->nameRange.start.line > 0 ? node->nameRange : node->range;
    sym.constant = node->constant;
    sym.documentation = std::string(node->global ? "global " : "") + (node->constant ? "const " : "") + node->type + " " + node->name;
    sym.fileUri = currentFile;
    addSymbol(node->global ? rootScope.get() : currentScope, sym, sym.declRange, !node->global);
    if (recursive) visitNode(node->expr.get());
}

void SemanticAnalyzer::checkVariable(const std::string& name, SourceRange range) {
    Symbol* sym = currentScope->find(name);
    if ((!sym || sym->kind == SymbolKind::Builtin || sym->kind == SymbolKind::Function) && !currentFuncReturnType.empty()) {
        auto global = fileGlobals.find(name);
        if (global != fileGlobals.end()) sym = &global->second;
    }
    if (!sym && findBuiltinSpec(name)) sym = &rootScope->symbols[name];
    // A function's name read as a value is the function itself: func f = twice;
    if (!sym) {
        diagnostics.push_back({DiagnosticSeverity::Error, "Undefined variable '" + name + "'", range});
    } else {
        symbolRefs.push_back({range, *sym});
    }
}

void SemanticAnalyzer::checkWritable(const std::string& name, SourceRange range) {
    const Symbol* variable = currentScope->find(name);
    if ((!variable || variable->kind == SymbolKind::Builtin || variable->kind == SymbolKind::Function) &&
        !currentFuncReturnType.empty()) {
        auto global = fileGlobals.find(name);
        if (global != fileGlobals.end()) variable = &global->second;
    }
    if (variable && variable->constant)
        diagnostics.push_back({DiagnosticSeverity::Error, "'" + name + "' is a constant and cannot be changed", range});
}

void SemanticAnalyzer::visitVarAssign(const VarAssignNode* node) {
    if (node->expr) visitNode(node->expr.get());
    SourceRange where = node->nameRange.start.line > 0 ? node->nameRange : node->range;
    checkVariable(node->name, where);
    checkWritable(node->name, where);
    if (const Symbol* variable = currentScope->find(node->name)) checkNarrowing(variable->type, node->expr.get());
    if (dynamic_cast<const NullNode*>(node->expr.get())) {
        const Symbol* variable = currentScope->find(node->name);
        if (!variable && !currentFuncReturnType.empty()) {
            auto global = fileGlobals.find(node->name);
            if (global != fileGlobals.end()) variable = &global->second;
        }
        if (variable && (variable->kind == SymbolKind::Variable || variable->kind == SymbolKind::Parameter) &&
            variable->type != "any" && !isNullable(variable->type))
            diagnostics.push_back({DiagnosticSeverity::Error,
                "Variable '" + node->name + "' of type " + variable->type + " cannot be null; declare it " + variable->type + "?", where});
    }
}

void SemanticAnalyzer::visitVarAccess(const VarAccessNode* node) {
    checkVariable(node->name, node->nameRange.start.line > 0 ? node->nameRange : node->range);
}

void SemanticAnalyzer::visitFuncCall(const FuncCallNode* node) {
    for (const auto& arg : node->args) visitNode(arg.get());

    Symbol* sym = currentScope->find(node->name);
    SourceRange targetRange = node->nameRange.start.line > 0 ? node->nameRange : node->range;
    const BuiltinSpec* builtin = findBuiltinSpec(node->name);
    Symbol* holder = sym;
    if ((!holder || holder->kind == SymbolKind::Builtin || holder->kind == SymbolKind::Function) && !currentFuncReturnType.empty()) {
        auto global = fileGlobals.find(node->name);
        if (global != fileGlobals.end()) holder = &global->second;
    }
    if (holder && (holder->kind == SymbolKind::Variable || holder->kind == SymbolKind::Parameter)) {
        // A variable holding a function is called like one; its arity is known only when it runs.
        symbolRefs.push_back({targetRange, *holder});
        if (holder->type != "func" && holder->type != "func?" && holder->type != "any")
            diagnostics.push_back({DiagnosticSeverity::Error,
                "'" + node->name + "' is a " + holder->type + " variable, not a function", targetRange});
        return;
    }
    if (sym && sym->kind == SymbolKind::Type && !builtin) {
        for (size_t i = 0; i < node->args.size() && i < sym->params.size(); ++i) checkNarrowing(sym->params[i].type, node->args[i].get());
        // Name(values...) builds a struct; trailing fields may be left out.
        symbolRefs.push_back({targetRange, *sym});
        if (node->args.size() > sym->params.size())
            diagnostics.push_back({DiagnosticSeverity::Error, "Struct '" + node->name + "' has " + std::to_string(sym->params.size()) +
                                   " fields, but got " + std::to_string(node->args.size()) + " values", node->range});
        return;
    }
    if (!sym || (sym->kind != SymbolKind::Function && sym->kind != SymbolKind::Builtin)) {
        if (builtin) {
            sym = &rootScope->symbols[node->name];
        } else {
            diagnostics.push_back({DiagnosticSeverity::Error, "Undefined function '" + node->name + "'", targetRange});
            return;
        }
    }
    symbolRefs.push_back({targetRange, *sym});
    // Name resolution is independent of the argument types.
    if (sym->kind == SymbolKind::Function) builtin = nullptr;
    if (sym->kind == SymbolKind::Function || sym->kind == SymbolKind::Builtin)
        for (size_t i = 0; i < node->args.size() && i < sym->params.size(); ++i)
            checkNarrowing(sym->params[i].type, node->args[i].get());

    if (sym->kind == SymbolKind::Function && !builtin) {
        for (size_t i = 0; i < node->args.size() && i < sym->params.size(); ++i) {
            const FuncParam& param = sym->params[i];
            if (dynamic_cast<const NullNode*>(node->args[i].get()) && !param.type.empty() && !isNullable(param.type))
                diagnostics.push_back({DiagnosticSeverity::Error,
                    "Parameter '" + param.name + "' of '" + node->name + "' has type " + param.type +
                    " and cannot be null; declare it " + param.type + "?", node->args[i]->range});
        }
    }

    // The selected declaration alone determines arity.
    size_t count = node->args.size();
    bool fitsFunction = sym->kind == SymbolKind::Function && sym->params.size() == count;
    bool fitsBuiltin = builtin && builtin->acceptsCount(count);
    if (!fitsFunction && !fitsBuiltin) {
        std::string expected = sym->kind == SymbolKind::Function ? std::to_string(sym->params.size())
                             : builtin->variadic ? "at least " + std::to_string(builtin->required)
                             : builtin->required == builtin->params.size() ? std::to_string(builtin->required)
                             : std::to_string(builtin->required) + " to " + std::to_string(builtin->params.size());
        diagnostics.push_back({DiagnosticSeverity::Error,
            "Function '" + node->name + "' expects " + expected + " arguments, but got " + std::to_string(count),
            node->range});
    }
}

void SemanticAnalyzer::visitLambda(const LambdaNode* node) {
    enterScope();
    std::string oldReturn = currentFuncReturnType;
    // A lambda may return a value or nothing: its type is known only when it runs.
    currentFuncReturnType = "any";
    for (const auto& param : node->params) {
        if (!param.type.empty()) checkType(param.type, param.range);
        Symbol paramSym;
        paramSym.name = param.name;
        paramSym.type = param.type.empty() ? "any" : param.type;
        paramSym.kind = SymbolKind::Parameter;
        paramSym.documentation = "parameter " + (param.type.empty() ? "" : param.type + " ") + param.name;
        paramSym.fileUri = currentFile;
        paramSym.declRange = param.range;
        currentScope->symbols[param.name] = paramSym;
        symbolRefs.push_back({param.range, paramSym});
    }
    if (node->body) visitBlock(node->body.get());
    currentFuncReturnType = oldReturn;
    exitScope();
}

void SemanticAnalyzer::visitReturn(const ReturnNode* node) {
    checkNarrowing(currentFuncReturnType, node->expr.get());
    if (node->expr) visitNode(node->expr.get());
    if (currentFuncReturnType == "void" && node->expr != nullptr) {
        diagnostics.push_back({DiagnosticSeverity::Error, "Void function should not return a value", node->range});
    } else if (currentFuncReturnType.empty()) {
        diagnostics.push_back({DiagnosticSeverity::Error, "'return' outside of a function", node->range});
    } else if (dynamic_cast<const NullNode*>(node->expr.get()) && currentFuncReturnType != "any" &&
               !isNullable(currentFuncReturnType)) {
        diagnostics.push_back({DiagnosticSeverity::Error,
            "Function returning " + currentFuncReturnType + " cannot return null; declare it " + currentFuncReturnType + "?", node->range});
    } else if (currentFuncReturnType != "void" && currentFuncReturnType != "any" && !isNullable(currentFuncReturnType) &&
               node->expr == nullptr) {
        diagnostics.push_back({DiagnosticSeverity::Error,
            "Function returning " + currentFuncReturnType + " must return a value", node->range});
    }
}

void SemanticAnalyzer::visitIf(const IfNode* node) {
    visitNode(node->condition.get());
    visitNode(node->thenB.get());
    visitNode(node->elseB.get());
}

void SemanticAnalyzer::visitWhile(const WhileNode* node) {
    visitNode(node->condition.get());
    visitNode(node->body.get());
}

void SemanticAnalyzer::visitFor(const ForNode* node) {
    enterScope();
    visitNode(node->init.get());
    visitNode(node->condition.get());
    visitNode(node->step.get());
    visitNode(node->body.get());
    exitScope();
}

void SemanticAnalyzer::visitForIn(const ForInNode* node) {
    visitNode(node->iterable.get());
    enterScope();
    for (const auto& variable : node->variables) {
        if (!variable.type.empty()) checkType(variable.type, variable.range);
        Symbol sym;
        sym.name = variable.name;
        sym.type = variable.type.empty() ? "any" : variable.type;
        sym.kind = SymbolKind::Variable;
        sym.declRange = variable.range;
        sym.documentation = (variable.type.empty() ? "" : variable.type + " ") + variable.name + " (переменная цикла)";
        sym.fileUri = currentFile;
        addSymbol(currentScope, sym, variable.range, true);
    }
    visitNode(node->body.get());
    exitScope();
}

void SemanticAnalyzer::visitSwitch(const SwitchNode* node) {
    visitNode(node->expr.get());
    for (const auto& c : node->cases) {
        visitNode(c.first.get());
        visitNode(c.second.get());
    }
    visitNode(node->defaultCase.get());
}

void SemanticAnalyzer::visitBinOp(const BinOpNode* node) {
    visitNode(node->left.get());
    visitNode(node->right.get());
}

void SemanticAnalyzer::visitArrayDecl(const ArrayDeclNode* node) {
    visitNode(node->initializer.get());
    checkNarrowing(node->type, node->initializer.get());
    checkType(node->type, node->nameRange.start.line > 0 ? node->nameRange : node->range);
    Symbol sym;
    sym.name = node->name;
    sym.type = node->type;
    sym.kind = SymbolKind::Variable;
    sym.declRange = node->nameRange.start.line > 0 ? node->nameRange : node->range;
    sym.constant = node->constant;
    sym.documentation = std::string(node->constant ? "const " : "") + node->type + " " + node->name;
    sym.fileUri = currentFile;
    addSymbol(node->global ? rootScope.get() : currentScope, sym, sym.declRange, !node->global);
}

void SemanticAnalyzer::visitUsing(const UsingNode* node) {
    std::string identity = loadModuleSymbols({node->libName, true}, node->range);
    if (!node->alias.empty()) {
        Symbol alias = aliasSymbol(node, identity);
        symbolRefs.push_back({node->aliasRange, alias});
        addSymbol(rootScope.get(), alias, node->aliasRange, true);
    }
    documentSymbols.push_back({"using " + node->libName, "Module", node->range, node->range, {}});
}

void SemanticAnalyzer::visitInclude(const IncludeNode* node) {
    loadModuleSymbols({node->filename, false}, node->range);
    documentSymbols.push_back({"include " + node->filename, "Module", node->range, node->range, {}});
}

const Symbol* SemanticAnalyzer::findFunction(const std::string& name) const {
    if (currentScope) {
        Symbol* s = currentScope->find(name);
        if (s && (s->kind == SymbolKind::Function || s->kind == SymbolKind::Builtin)) {
            return s;
        }
    }
    if (rootScope) {
        auto it = rootScope->symbols.find(name);
        if (it != rootScope->symbols.end()) {
            if (it->second.kind == SymbolKind::Function || it->second.kind == SymbolKind::Builtin) {
                return &it->second;
            }
        }
    }
    return nullptr;
}

SignatureHelpResult SemanticAnalyzer::getSignatureHelp(const std::string& code, int line, int col) const {
    // LSP columns count UTF-16 units; source offsets count UTF-8 bytes.
    const size_t offset = utf::lspPositionToByteOffset(code, line - 1, col - 1);
    // Reuse the lexer so quotes, escapes, comments and incomplete strings follow
    // the same rules as the language. Punctuation inside strings is never an argument.
    Lexer lexer(code.substr(0, offset), true);
    const auto tokens = lexer.tokenize();
    struct CallFrame { std::string name; int argument = 0; };
    std::vector<CallFrame> calls;
    for (size_t i = 0; i < tokens.size(); ++i) {
        switch (tokens[i].type) {
        case TokenType::LPAREN: {
            std::string name;
            if (i > 0 && findFunction(tokens[i - 1].value)) name = tokens[i - 1].value;
            calls.push_back({std::move(name), 0});
            break;
        }
        case TokenType::RPAREN:
            if (!calls.empty()) calls.pop_back();
            break;
        case TokenType::COMMA:
            if (!calls.empty()) ++calls.back().argument;
            break;
        case TokenType::SEMICOLON:
        case TokenType::LBRACE:
        case TokenType::RBRACE:
            calls.clear();
            break;
        default: break;
        }
    }
    // A grouping expression has no signature; use the surrounding function call.
    while (!calls.empty() && calls.back().name.empty()) calls.pop_back();
    if (calls.empty()) return {};
    const Symbol* fnSym = findFunction(calls.back().name);
    if (!fnSym) return {};
    const int commaCount = calls.back().argument;

    SignatureHelpResult result;
    result.found = true;
    result.activeSignature = 0;
    result.activeParameter = commaCount;

    SignatureInfo sig;
    for (const auto& param : fnSym->params) {
        ParameterInfo paramInfo;
        paramInfo.label = (param.type.empty() ? "" : param.type + " ") + param.name;
        paramInfo.documentation = "Параметр `" + param.name + "` (" + (param.type.empty() ? "any" : param.type) + ")";
        sig.parameters.push_back(std::move(paramInfo));
    }
    // Every further argument of a variadic builtin belongs to its last parameter.
    const BuiltinSpec* spec = fnSym->kind == SymbolKind::Builtin ? findBuiltinSpec(fnSym->name) : nullptr;
    if (spec && spec->variadic && !sig.parameters.empty())
        result.activeParameter = std::min(commaCount, static_cast<int>(sig.parameters.size()) - 1);
    sig.label = signatureOf(*fnSym);
    sig.documentation = fnSym->documentation;

    result.signatures.push_back(std::move(sig));
    return result;
}

HoverInfo SemanticAnalyzer::getHover(int line, int col, const std::string& code) const {
    const SymbolRef* best = nullptr;
    for (const auto& ref : symbolRefs) {
        if (ref.range.contains(line, col) &&
            !(line == ref.range.end.line && col == ref.range.end.column)) {
            // Find most specific (innermost) match
            if (!best || (ref.range.end.line - ref.range.start.line < best->range.end.line - best->range.start.line) ||
                (ref.range.end.column - ref.range.start.column < best->range.end.column - best->range.start.column)) {
                best = &ref;
            }
        }
    }

    if (!best) {
        // Syntax has no symbol reference, but still deserves editor help.
        Lexer lexer(code, true);
        const auto tokens = lexer.tokenize();
        const size_t offset = utf::lspPositionToByteOffset(code, line - 1, col - 1);
        static const std::unordered_map<std::string, std::string> operators = {
            {"+", "Сложение чисел или объединение строк."}, {"-", "Вычитание или изменение знака числа."},
            {"*", "Умножение чисел."}, {"/", "Деление чисел."}, {"%", "Остаток от деления."},
            {"++", "Постфиксное увеличение переменной на единицу: i++."},
            {"--", "Постфиксное уменьшение переменной на единицу: i--."},
            {"%=", "Остаток от деления с присваиванием."},
            {"=", "Присваивание значения переменной."},
            {"+=", "Сложение с присваиванием."}, {"-=", "Вычитание с присваиванием."},
            {"*=", "Умножение с присваиванием."}, {"/=", "Деление с присваиванием."},
            {"==", "Проверка равенства."}, {"!=", "Проверка неравенства."},
            {"<", "Сравнение: меньше."}, {">", "Сравнение: больше."},
            {"<=", "Сравнение: меньше или равно."}, {">=", "Сравнение: больше или равно."},
            {"&&", "Логическое И."}, {"||", "Логическое ИЛИ."}, {"!", "Логическое отрицание."}
        };
        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& token = tokens[i];
            if (offset < token.range.start.byteOffset || offset >= token.range.end.byteOffset) continue;
            if (token.type == TokenType::STRING_LITERAL || token.type == TokenType::NUMBER) return {};
            auto op = operators.find(token.value);
            if (op != operators.end())
                return {"`" + token.value + "`\n\n" + op->second, token.range, true};
            for (const auto& item : getCompletions(line, col)) {
                if (item.label == token.value && (item.kind == "Keyword" ||
                    (item.kind == "Function" && i + 1 < tokens.size() && tokens[i + 1].type == TokenType::LPAREN) ||
                    (item.kind == "Module" && i > 0 && tokens[i - 1].type == TokenType::USING)))
                    return {"```skrumi\n" + item.detail + "\n```\n\n---\n" + item.documentation, token.range, true};
            }
            return {};
        }
        return {};
    }

    HoverInfo info;
    info.found = true;
    info.range = best->range;
    std::ostringstream ss;
    ss << "```skrumi\n";
    if (best->symbol.kind == SymbolKind::Function || best->symbol.kind == SymbolKind::Builtin) {
        ss << "(function) " << signatureOf(best->symbol);
    } else {
        ss << "(variable) " << best->symbol.type << " " << best->symbol.name;
    }
    ss << "\n```";
    if (!best->symbol.documentation.empty() && best->symbol.documentation != best->symbol.name) {
        ss << "\n\n---\n" << best->symbol.documentation;
    }
    info.markdown = ss.str();
    return info;
}

SymbolIdentity SemanticAnalyzer::symbolAt(int line, int col) const {
    SymbolIdentity identity;
    for (const auto& ref : symbolRefs) {
        if (!ref.range.contains(line, col)) continue;
        const Symbol& symbol = ref.symbol;
        identity.found = true;
        identity.name = symbol.name;
        identity.file = symbol.fileUri.empty() ? currentFile : symbol.fileUri;
        identity.declaration = symbol.declRange;
        identity.at = ref.range;
        identity.editable = symbol.kind != SymbolKind::Builtin && symbol.kind != SymbolKind::Keyword &&
                            symbol.kind != SymbolKind::Module && identity.file.rfind("@", 0) != 0 &&
                            symbol.declRange.start.line > 0;
        return identity;
    }
    return identity;
}

std::vector<SourceRange> SemanticAnalyzer::referencesTo(const SymbolIdentity& target) const {
    std::vector<SourceRange> ranges;
    for (const auto& ref : symbolRefs) {
        const Symbol& symbol = ref.symbol;
        std::string file = symbol.fileUri.empty() ? currentFile : symbol.fileUri;
        if (symbol.name != target.name || file != target.file) continue;
        if (symbol.declRange.start.line != target.declaration.start.line ||
            symbol.declRange.start.column != target.declaration.start.column) continue;
        bool seen = std::any_of(ranges.begin(), ranges.end(), [&](const SourceRange& r) {
            return r.start.line == ref.range.start.line && r.start.column == ref.range.start.column;
        });
        if (!seen) ranges.push_back(ref.range);
    }
    return ranges;
}

DefinitionInfo SemanticAnalyzer::getDefinition(int line, int col) const {
    for (const auto& ref : symbolRefs) {
        if (ref.range.contains(line, col)) {
            if (ref.symbol.declRange.start.line > 0) {
                DefinitionInfo info;
                info.found = true;
                info.range = ref.symbol.declRange;
                info.fileUri = ref.symbol.fileUri.empty() ? currentFile : ref.symbol.fileUri;
                return info;
            }
        }
    }
    return {};
}

std::vector<CompletionItem> SemanticAnalyzer::getMemberCompletions(const std::string& name, int line, int col) const {
    // The declaration of `name` nearest before the cursor gives its type.
    const Symbol* variable = nullptr;
    SourcePosition best{0, 0};
    for (const auto& ref : symbolRefs) {
        const Symbol& symbol = ref.symbol;
        if (symbol.name != name || (symbol.kind != SymbolKind::Variable && symbol.kind != SymbolKind::Parameter &&
                                    symbol.kind != SymbolKind::Module && !symbol.isEnum)) continue;
        SourcePosition at = ref.range.start;
        bool before = at.line < line || (at.line == line && at.column <= col);
        bool later = at.line > best.line || (at.line == best.line && at.column >= best.column);
        if (before && later) {
            variable = &symbol;
            best = at;
        }
    }
    std::vector<CompletionItem> items;
    if (!variable) return items;
    if (variable->isEnum && variable->kind == SymbolKind::Type) {
        for (const auto& value : variable->methods) items.push_back({value.name, "EnumMember", value.documentation, ""});
        return items;
    }
    if (variable->kind == SymbolKind::Module) {
        for (const auto& member : variable->methods)
            items.push_back({member.name, member.kind == SymbolKind::Function ? "Function" : "Variable",
                             member.kind == SymbolKind::Function ? signatureOf(member.name, member.params, member.returnType)
                                                                 : member.type + " " + member.name,
                             member.documentation});
        return items;
    }
    std::string declared = isNullable(variable->type) ? variable->type.substr(0, variable->type.size() - 1) : variable->type;
    const Symbol* type = rootScope ? rootScope->find(declared) : nullptr;
    if (!type || type->kind != SymbolKind::Type) return items;
    for (const auto& field : type->params)
        items.push_back({field.name, "Field", field.type + " " + type->name + "." + field.name, ""});
    for (const auto& method : type->methods)
        items.push_back({method.name, "Method", method.documentation, ""});
    return items;
}

std::vector<CompletionItem> SemanticAnalyzer::getCompletions(int line, int col) const {
    (void)line;
    (void)col;
    std::vector<CompletionItem> items;
    std::unordered_set<std::string> seen;

    auto add = [&](std::string label, std::string kind, std::string detail, std::string doc) {
        // A module and a function may share a name: `using log;` and log(x).
        if (seen.insert(kind + ":" + label).second) {
            items.push_back({std::move(label), std::move(kind), std::move(detail), std::move(doc)});
        }
    };

    // 1. Language keywords and directives
    struct KeywordDoc {
        const char* kw;
        const char* detail;
        const char* doc;
    };
    static const std::vector<KeywordDoc> kwDocs = {
        {"if", "(keyword) if (cond) { ... }", "Условный оператор ветвления `if / else`.\n\n```skrumi\nif (условие) {\n    // истина\n} else {\n    // иначе\n}\n```"},
        {"else", "(keyword) else", "Ветка `else` для оператора ветвления `if`.\n\n```skrumi\nif (cond) {\n    ...\n} else {\n    ...\n}\n```"},
        {"while", "(keyword) while (cond) { ... }", "Цикл с предусловием `while`.\n\n```skrumi\nwhile (условие) {\n    // тело цикла\n}\n```"},
        {"for", "(keyword) for (init; cond; step) { ... }", "Цикл со счётчиком `for`.\n\n```skrumi\nfor (int i = 0; i < 10; i++) {\n    print(i);\n}\n```"},
        {"switch", "(keyword) switch (val) { case ... }", "Оператор множественного выбора `switch / case / default`. Без `break` выполнение переходит в следующую ветку.\n\n```skrumi\nswitch (day) {\n    case 1:\n        print(\"пн\");\n        break;\n    default:\n        print(\"другой\");\n}\n```"},
        {"case", "(keyword) case value:", "Ветка выбора `case` внутри оператора `switch`."},
        {"default", "(keyword) default:", "Ветка по умолчанию `default` внутри `switch`."},
        {"break", "(keyword) break;", "Прерывание выполнения текущего цикла или оператора `switch`."},
        {"continue", "(keyword) continue;", "Переход к следующей итерации цикла."},
        {"return", "(keyword) return [value];", "Возврат значения из функции или выход из `void` функции."},
        {"using", "(keyword) using <module>;", "Директива подключения стандартной библиотеки Skrumi.\n\n```skrumi\nusing server;\nusing http;\nusing env;\nusing log;\nusing json;\nusing string;\n```"},
        {"include", "(keyword) include(\"path.skm\");", "Директива подключения пользовательского файла с кодом.\n\n```skrumi\ninclude(\"helper.skm\");\n```"},
        {"global", "(keyword) global type name = val;", "Объявление глобальной переменной в Skrumi.\n\n```skrumi\nglobal string token = secret(\"API_KEY\");\n```"},
        {"int", "(type) int", "32-битное целое число со знаком."},
        {"float", "(type) float", "Дробное число с плавающей точкой."},
        {"string", "(type) string", "Текстовая строка с поддержкой UTF-8 и Unicode эмодзи."},
        {"bool", "(type) bool", "Логический тип данных: `true` или `false`."},
        {"void", "(type) void", "Тип отсутствия возвращаемого значения функции."},
        {"func", "(type) func", "Функция как значение: лямбда `(int x) => x * 2`, имя функции или встроенной функции. "
            "Переменную типа `func` вызывают как функцию.\n\n```skrumi\nfunc twice = (int x) => x * 2;\nprint(twice(21));\n```"},
        {"true", "(keyword) true", "Логическая истина."},
        {"enum", "(keyword) enum Name { A, B, C }", "Перечисление: тип с фиксированным набором значений `Name.A`, у каждого есть "
            "`.name` и `.value`.\n\n```skrumi\nenum Color { Red, Green, Blue }\nColor c = Color.Green;\n```"},
        {"const", "(keyword) const type name = value;", "Константа: имя, которое нельзя присвоить заново. "
            "Содержимое массива, словаря или структуры менять можно.\n\n```skrumi\nconst int MAX_USERS = 100;\n```"},
        {"null", "(keyword) null", "«Нет значения». Хранится только в переменных, параметрах, полях и результатах типа `T?`: "
            "`string? nick = null;`. `a ?? b` — `a`, если оно не `null`, иначе `b`; `user?.name` — `null` вместо ошибки, когда `user` — `null`."},
        {"this", "(keyword) this", "Внутри метода структуры — значение, у которого метод вызван: `this.x`."},
        {"false", "(keyword) false", "Логическая ложь."},
        {"array", "(keyword) array <name> [size | = value];", "Массив: `array имя размер;`, `array имя = [1, 2, 3];` или пустой `array имя;`. "
            "Элементы читаются и пишутся как `имя[i]`, размер меняют `push`, `pop` и `resize`. Тип `array` допустим у параметров и результата функции."}
    };
    for (const auto& kd : kwDocs) {
        add(kd.kw, "Keyword", kd.detail, kd.doc);
    }

    // 2. Standard library modules, described by their own `//!` comments
    for (const auto& module : standardModules()) {
        add(module.name, "Module", "(module) using " + module.name + ";", module.documentation);
    }

    // 3. All visible symbols from root scope and symbol refs
    std::vector<Symbol> allSymbols;
    rootScope->getAllSymbols(allSymbols);
    for (const auto& ref : symbolRefs) {
        allSymbols.push_back(ref.symbol);
    }

    for (const auto& sym : allSymbols) {
        std::string kindStr = (sym.kind == SymbolKind::Function || sym.kind == SymbolKind::Builtin) ? "Function" : "Variable";
        std::string detail;
        if (sym.kind == SymbolKind::Function || sym.kind == SymbolKind::Builtin) {
            detail = "(function) " + signatureOf(sym);
        } else {
            detail = "(variable) " + sym.type + " " + sym.name;
        }
        add(sym.name, kindStr, detail, sym.documentation);
    }

    return items;
}

std::vector<DocumentSymbolInfo> SemanticAnalyzer::getDocumentSymbols() const {
    return documentSymbols;
}

} // namespace skrumi
