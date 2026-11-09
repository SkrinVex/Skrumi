#include "LspServer.h"
#include "skrumi/Skrumi.h"
#include "skrumi/Formatter.h"
#include <cctype>
#include <iostream>

namespace skrumi {
namespace lsp {

LspServer::LspServer(Transport& t) : transport(t) {}

int LspServer::run(std::istream& in, std::ostream& out) {
    std::string message;
    while (!exitRequested && transport.readMessage(in, message)) {
        processMessage(message, out);
    }
    return 0;
}

void LspServer::processMessage(const std::string& rawMessage, std::ostream& out) {
    try {
        JsonValue msg = JsonValue::parse(rawMessage);
        if (!msg.isObject()) return;

        if (msg.has("id")) {
            handleRequest(msg, out);
        } else if (msg.has("method")) {
            handleNotification(msg, out);
        }
    } catch (const std::exception& e) {
        Transport::log(std::string("Exception processing message: ") + e.what());
    }
}

void LspServer::handleRequest(const JsonValue& msg, std::ostream& out) {
    std::string method = msg.get("method").asString();
    JsonValue id = msg.get("id");

    if (method == "initialize") {
        // The workspace root bounds the search for the other files of a program.
        const auto& params = msg.get("params");
        std::string root;
        if (params.get("workspaceFolders").isArray() && !params.get("workspaceFolders").arrayValue.empty())
            root = params.get("workspaceFolders")[0].get("uri").asString();
        if (root.empty() && params.get("rootUri").isString()) root = params.get("rootUri").asString();
        if (root.empty() && params.get("rootPath").isString()) root = DocumentManager::filePathToUri(params.get("rootPath").asString());
        docManager.setWorkspaceRoot(root);

        std::map<std::string, JsonValue> capabilities;
        capabilities["codeActionProvider"] = true;
        // Full document sync; saves are reported because a saved file can change
        // what the other files of its program see.
        std::map<std::string, JsonValue> sync;
        sync["openClose"] = true;
        sync["change"] = 1;
        std::map<std::string, JsonValue> save;
        save["includeText"] = false;
        sync["save"] = JsonValue(save);
        capabilities["textDocumentSync"] = JsonValue(sync);

        std::map<std::string, JsonValue> compProvider;
        compProvider["resolveProvider"] = false;
        std::vector<JsonValue> triggers;
        triggers.push_back(JsonValue("."));
        triggers.push_back(JsonValue(" "));
        compProvider["triggerCharacters"] = JsonValue(triggers);
        capabilities["completionProvider"] = JsonValue(compProvider);

        std::map<std::string, JsonValue> sigProvider;
        std::vector<JsonValue> sigTriggers;
        sigTriggers.push_back(JsonValue("("));
        sigTriggers.push_back(JsonValue(","));
        sigProvider["triggerCharacters"] = JsonValue(sigTriggers);
        capabilities["signatureHelpProvider"] = JsonValue(sigProvider);

        capabilities["hoverProvider"] = true;
        capabilities["definitionProvider"] = true;
        capabilities["documentSymbolProvider"] = true;
        capabilities["referencesProvider"] = true;
        std::map<std::string, JsonValue> renameProvider;
        renameProvider["prepareProvider"] = true;
        capabilities["renameProvider"] = JsonValue(renameProvider);
        capabilities["documentFormattingProvider"] = true;

        std::map<std::string, JsonValue> serverInfo;
        serverInfo["name"] = "skrumi-lsp";
        serverInfo["version"] = skrumi::Interpreter::getVersion();

        std::map<std::string, JsonValue> res;
        res["capabilities"] = JsonValue(capabilities);
        res["serverInfo"] = JsonValue(serverInfo);

        sendResponse(id, JsonValue(res), out);
        isInitialized = true;
        return;
    }

    if (method == "shutdown") {
        isShutdown = true;
        sendResponse(id, JsonValue(), out);
        return;
    }

    if (method == "textDocument/completion") {
        const auto& params = msg.get("params");
        std::string uri = params.get("textDocument").get("uri").asString();
        LspPosition pos = LspPosition::fromJson(params.get("position"));
        const auto* doc = docManager.getDocument(uri);
        std::vector<JsonValue> items;
        if (doc && doc->analyzer) {
            int line = utf::lspLineToLine(pos.line);
            int col = utf::lspCharacterToColumn(pos.character);
            // After `name.` only the fields and methods of name's struct fit.
            std::vector<CompletionItem> compItems;
            size_t cursor = utf::lspPositionToByteOffset(doc->text, pos.line, pos.character);
            auto word = [&](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
            size_t dot = cursor;
            while (dot > 0 && word(doc->text[dot - 1])) --dot;
            if (dot > 0 && doc->text[dot - 1] == '.') {
                size_t end = dot - 1, begin = end;
                while (begin > 0 && word(doc->text[begin - 1])) --begin;
                if (begin < end) compItems = doc->analyzer->getMemberCompletions(doc->text.substr(begin, end - begin), line, col);
            }
            if (compItems.empty()) compItems = doc->analyzer->getCompletions(line, col);
            for (const auto& ci : compItems) {
                int kindVal = 1;
                if (ci.kind == "Keyword") kindVal = 14;
                else if (ci.kind == "Field") kindVal = 5;
                else if (ci.kind == "Method") kindVal = 2;
                else if (ci.kind == "Function") kindVal = 3;
                else if (ci.kind == "Variable") kindVal = 6;
                else if (ci.kind == "Type") kindVal = 7;
                else if (ci.kind == "Module") kindVal = 9;
                else if (ci.kind == "EnumMember") kindVal = 20;
                LspCompletionItem lci;
                lci.label = ci.label;
                lci.kind = kindVal;
                lci.detail = ci.detail;
                lci.documentation = ci.documentation;
                items.push_back(lci.toJson());
            }
        }
        sendResponse(id, JsonValue(items), out);
        return;
    }

    if (method == "textDocument/signatureHelp") {
        const auto& params = msg.get("params");
        std::string uri = params.get("textDocument").get("uri").asString();
        LspPosition pos = LspPosition::fromJson(params.get("position"));
        const auto* doc = docManager.getDocument(uri);
        if (doc && doc->analyzer) {
            int line = utf::lspLineToLine(pos.line);
            int col = utf::lspCharacterToColumn(pos.character);
            auto sigHelp = doc->analyzer->getSignatureHelp(doc->text, line, col);
            if (sigHelp.found) {
                std::vector<JsonValue> sigs;
                for (const auto& s : sigHelp.signatures) {
                    std::map<std::string, JsonValue> sigObj;
                    sigObj["label"] = s.label;
                    if (!s.documentation.empty()) {
                        std::map<std::string, JsonValue> docObj;
                        docObj["kind"] = "markdown";
                        docObj["value"] = s.documentation;
                        sigObj["documentation"] = JsonValue(docObj);
                    }
                    std::vector<JsonValue> paramsList;
                    for (const auto& p : s.parameters) {
                        std::map<std::string, JsonValue> pObj;
                        pObj["label"] = p.label;
                        if (!p.documentation.empty()) {
                            std::map<std::string, JsonValue> pDoc;
                            pDoc["kind"] = "markdown";
                            pDoc["value"] = p.documentation;
                            pObj["documentation"] = JsonValue(pDoc);
                        }
                        paramsList.push_back(JsonValue(pObj));
                    }
                    sigObj["parameters"] = JsonValue(paramsList);
                    sigs.push_back(JsonValue(sigObj));
                }
                std::map<std::string, JsonValue> res;
                res["signatures"] = JsonValue(sigs);
                res["activeSignature"] = sigHelp.activeSignature;
                res["activeParameter"] = sigHelp.activeParameter;
                sendResponse(id, JsonValue(res), out);
                return;
            }
        }
        sendResponse(id, JsonValue(), out);
        return;
    }

    if (method == "textDocument/hover") {
        const auto& params = msg.get("params");
        std::string uri = params.get("textDocument").get("uri").asString();
        LspPosition pos = LspPosition::fromJson(params.get("position"));
        const auto* doc = docManager.getDocument(uri);
        if (doc && doc->analyzer) {
            int line = utf::lspLineToLine(pos.line);
            int col = utf::lspCharacterToColumn(pos.character);
            auto hover = doc->analyzer->getHover(line, col, doc->text);
            if (hover.found) {
                std::map<std::string, JsonValue> res;
                std::map<std::string, JsonValue> contents;
                contents["kind"] = "markdown";
                contents["value"] = hover.markdown;
                res["contents"] = JsonValue(contents);
                res["range"] = LspRange::fromSourceRange(hover.range).toJson();
                sendResponse(id, JsonValue(res), out);
                return;
            }
        }
        sendResponse(id, JsonValue(), out);
        return;
    }

    if (method == "textDocument/definition") {
        const auto& params = msg.get("params");
        std::string uri = params.get("textDocument").get("uri").asString();
        LspPosition pos = LspPosition::fromJson(params.get("position"));
        const auto* doc = docManager.getDocument(uri);
        if (doc && doc->analyzer) {
            int line = utf::lspLineToLine(pos.line);
            int col = utf::lspCharacterToColumn(pos.character);
            auto def = doc->analyzer->getDefinition(line, col);
            if (def.found) {
                std::string targetUri = def.fileUri.empty() ? uri : DocumentManager::filePathToUri(def.fileUri);
                LspLocation loc;
                loc.uri = targetUri;
                loc.range = LspRange::fromSourceRange(def.range);
                sendResponse(id, loc.toJson(), out);
                return;
            }
        }
        sendResponse(id, JsonValue(), out);
        return;
    }

    if (method == "textDocument/references" || method == "textDocument/rename" || method == "textDocument/prepareRename") {
        const auto& params = msg.get("params");
        std::string uri = params.get("textDocument").get("uri").asString();
        LspPosition pos = LspPosition::fromJson(params.get("position"));
        const auto* doc = docManager.getDocument(uri);
        SymbolIdentity target;
        if (doc && doc->analyzer)
            target = doc->analyzer->symbolAt(utf::lspLineToLine(pos.line), utf::lspCharacterToColumn(pos.character));
        if (method == "textDocument/prepareRename") {
            if (!target.found || !target.editable) {
                sendError(id, -32602, target.found ? "Встроенную функцию или функцию стандартной библиотеки переименовать нельзя"
                                                   : "Здесь нет имени, которое можно переименовать", out);
                return;
            }
            std::map<std::string, JsonValue> res;
            res["range"] = LspRange::fromSourceRange(target.at).toJson();
            res["placeholder"] = target.name;
            sendResponse(id, JsonValue(res), out);
            return;
        }
        if (!target.found) {
            sendResponse(id, method == "textDocument/references" ? JsonValue(std::vector<JsonValue>{}) : JsonValue(), out);
            return;
        }
        std::string newName = params.get("newName").asString();
        if (method == "textDocument/rename") {
            bool identifier = !newName.empty() && !std::isdigit(static_cast<unsigned char>(newName[0]));
            for (char c : newName) identifier = identifier && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
            for (const char* const* keyword = keywordList(); *keyword; ++keyword) identifier = identifier && newName != *keyword;
            if (!target.editable || !identifier) {
                sendError(id, -32602, !target.editable ? "Встроенную функцию или функцию стандартной библиотеки переименовать нельзя"
                                                       : "«" + newName + "» не подходит как имя: латинские буквы, цифры и _, не ключевое слово", out);
                return;
            }
        }
        bool includeDeclaration = params.get("context").get("includeDeclaration").asBool(true);
        std::vector<JsonValue> locations;
        std::map<std::string, JsonValue> changes;
        docManager.forEachProgramFile(uri, [&](const std::string& fileUri, const SemanticAnalyzer& analyzer) {
            std::vector<JsonValue> edits;
            std::string filePath = canonicalPath(DocumentManager::uriToFilePath(fileUri));
            for (const auto& range : analyzer.referencesTo(target)) {
                bool declaration = filePath == target.file && range.start.line == target.declaration.start.line &&
                                   range.start.column == target.declaration.start.column;
                if (method == "textDocument/references") {
                    if (declaration && !includeDeclaration) continue;
                    LspLocation location;
                    location.uri = fileUri;
                    location.range = LspRange::fromSourceRange(range);
                    locations.push_back(location.toJson());
                } else {
                    std::map<std::string, JsonValue> edit;
                    edit["range"] = LspRange::fromSourceRange(range).toJson();
                    edit["newText"] = newName;
                    edits.push_back(JsonValue(edit));
                }
            }
            if (!edits.empty()) changes[fileUri] = JsonValue(edits);
        });
        if (method == "textDocument/references") {
            sendResponse(id, JsonValue(locations), out);
        } else {
            std::map<std::string, JsonValue> workspaceEdit;
            workspaceEdit["changes"] = JsonValue(changes);
            sendResponse(id, JsonValue(workspaceEdit), out);
        }
        return;
    }

    if (method == "textDocument/codeAction") {
        const auto& params = msg.get("params");
        std::string uri = params.get("textDocument").get("uri").asString();
        auto requested = LspRange::fromJson(params.get("range"));
        const auto* doc = docManager.getDocument(uri);
        std::vector<JsonValue> actions;
        if (doc && doc->analyzer) for (const auto& diagnostic : doc->diagnostics) {
            if (diagnostic.code != 2001 || diagnostic.range.end.line < requested.start.line ||
                diagnostic.range.start.line > requested.end.line) continue;
            auto target = doc->analyzer->symbolAt(utf::lspLineToLine(diagnostic.range.start.line),
                                                utf::lspCharacterToColumn(diagnostic.range.start.character));
            if (!target.found || !target.editable) continue;
            // Check every affected scope before suggesting a name: an inner local
            // must not capture a reference when an outer function is renamed.
            std::set<std::string> occupied;
            std::map<std::string, std::vector<SourceRange>> references;
            docManager.forEachProgramFile(uri, [&](const std::string& file, const SemanticAnalyzer& analyzer) {
                auto ranges = analyzer.referencesTo(target);
                for (const auto& range : ranges)
                    for (const auto& item : analyzer.getCompletions(range.start.line, range.start.column))
                        occupied.insert(item.label);
                if (!ranges.empty()) references[file] = std::move(ranges);
            });
            std::string name = target.name + "_local";
            for (int suffix = 2; occupied.count(name); ++suffix) name = target.name + "_local" + std::to_string(suffix);
            std::map<std::string, JsonValue> changes;
            for (const auto& entry : references) {
                std::vector<JsonValue> edits;
                for (const auto& range : entry.second)
                    edits.emplace_back(std::map<std::string, JsonValue>{{"range", LspRange::fromSourceRange(range).toJson()}, {"newText", name}});
                changes[entry.first] = JsonValue(edits);
            }
            if (changes.empty()) continue;
            actions.emplace_back(std::map<std::string, JsonValue>{
                {"title", "Переименовать '" + target.name + "' в '" + name + "'"},
                {"kind", "quickfix"}, {"diagnostics", JsonValue(std::vector<JsonValue>{diagnostic.toJson()})},
                {"edit", JsonValue(std::map<std::string, JsonValue>{{"changes", JsonValue(changes)}})}});
        }
        sendResponse(id, JsonValue(actions), out);
        return;
    }

    if (method == "textDocument/formatting") {
        std::string uri = msg.get("params").get("textDocument").get("uri").asString();
        const auto* doc = docManager.getDocument(uri);
        std::vector<JsonValue> edits;
        if (doc) {
            std::string formatted = formatSource(doc->text);
            if (formatted != doc->text) {
                // One edit replaces the whole document: from the start past its last line.
                int lines = 1;
                for (char c : doc->text) lines += c == '\n';
                std::map<std::string, JsonValue> start, end, range, edit;
                start["line"] = 0;
                start["character"] = 0;
                end["line"] = lines;
                end["character"] = 0;
                range["start"] = JsonValue(start);
                range["end"] = JsonValue(end);
                edit["range"] = JsonValue(range);
                edit["newText"] = formatted;
                edits.push_back(JsonValue(edit));
            }
        }
        sendResponse(id, JsonValue(edits), out);
        return;
    }

    if (method == "textDocument/documentSymbol") {
        const auto& params = msg.get("params");
        std::string uri = params.get("textDocument").get("uri").asString();
        const auto* doc = docManager.getDocument(uri);
        std::vector<JsonValue> syms;
        if (doc && doc->analyzer) {
            auto docSyms = doc->analyzer->getDocumentSymbols();
            for (const auto& ds : docSyms) {
                int kindVal = 13; // Variable
                if (ds.kind == "Function") kindVal = 12;
                else if (ds.kind == "Module") kindVal = 2;
                else if (ds.kind == "Struct") kindVal = 23;
                else if (ds.kind == "Enum") kindVal = 10;
                else if (ds.kind == "Method") kindVal = 6;
                LspDocumentSymbol lds;
                lds.name = ds.name;
                lds.kind = kindVal;
                lds.range = LspRange::fromSourceRange(ds.range);
                lds.selectionRange = LspRange::fromSourceRange(ds.selectionRange);
                syms.push_back(lds.toJson());
            }
        }
        sendResponse(id, JsonValue(syms), out);
        return;
    }

    // Default unknown method response
    sendError(id, -32601, "Method not found: " + method, out);
}

void LspServer::handleNotification(const JsonValue& msg, std::ostream& out) {
    std::string method = msg.get("method").asString();

    if (method == "initialized") {
        // Notification from client after initialize response
        return;
    }

    if (method == "exit") {
        exitRequested = true;
        return;
    }

    if (method == "textDocument/didOpen") {
        const auto& td = msg.get("params").get("textDocument");
        std::string uri = td.get("uri").asString();
        std::string text = td.get("text").asString();
        int version = td.get("version").asInt(1);
        for (const auto& changed : docManager.openDocument(uri, text, version)) publishDiagnostics(changed, out);
        return;
    }

    if (method == "textDocument/didChange") {
        const auto& params = msg.get("params");
        const auto& td = params.get("textDocument");
        std::string uri = td.get("uri").asString();
        int version = td.get("version").asInt(1);
        const auto& changes = params.get("contentChanges");
        if (changes.isArray() && !changes.arrayValue.empty()) {
            std::string text = changes[0].get("text").asString();
            for (const auto& changed : docManager.updateDocument(uri, text, version)) publishDiagnostics(changed, out);
        }
        return;
    }

    if (method == "textDocument/didSave") {
        for (const auto& changed : docManager.refresh()) publishDiagnostics(changed, out);
        return;
    }

    if (method == "textDocument/didClose") {
        std::string uri = msg.get("params").get("textDocument").get("uri").asString();
        for (const auto& changed : docManager.closeDocument(uri)) publishDiagnostics(changed, out);
        // Clear diagnostics on close
        std::map<std::string, JsonValue> params;
        params["uri"] = uri;
        params["diagnostics"] = JsonValue(std::vector<JsonValue>{});
        sendNotification("textDocument/publishDiagnostics", JsonValue(params), out);
        return;
    }
}

void LspServer::sendResponse(const JsonValue& id, const JsonValue& result, std::ostream& out) {
    std::map<std::string, JsonValue> res;
    res["jsonrpc"] = "2.0";
    res["id"] = id;
    res["result"] = result;
    transport.sendMessage(out, JsonValue(res).serialize());
}

void LspServer::sendError(const JsonValue& id, int code, const std::string& message, std::ostream& out) {
    std::map<std::string, JsonValue> errObj;
    errObj["code"] = code;
    errObj["message"] = message;

    std::map<std::string, JsonValue> res;
    res["jsonrpc"] = "2.0";
    res["id"] = id;
    res["error"] = JsonValue(errObj);
    transport.sendMessage(out, JsonValue(res).serialize());
}

void LspServer::sendNotification(const std::string& method, const JsonValue& params, std::ostream& out) {
    std::map<std::string, JsonValue> notif;
    notif["jsonrpc"] = "2.0";
    notif["method"] = method;
    notif["params"] = params;
    transport.sendMessage(out, JsonValue(notif).serialize());
}

void LspServer::publishDiagnostics(const std::string& uri, std::ostream& out) {
    auto diags = docManager.getDiagnostics(uri);
    std::vector<JsonValue> diagJson;
    for (const auto& d : diags) {
        diagJson.push_back(d.toJson());
    }
    std::map<std::string, JsonValue> params;
    params["uri"] = uri;
    params["diagnostics"] = JsonValue(diagJson);
    sendNotification("textDocument/publishDiagnostics", JsonValue(params), out);
}

} // namespace lsp
} // namespace skrumi
