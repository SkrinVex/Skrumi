#pragma once
#include <string>
#include <vector>
#include "Json.h"
#include "skrumi/SourceLocation.h"

namespace skrumi {
namespace lsp {

struct LspPosition {
    int line = 0;       // 0-based
    int character = 0;  // 0-based UTF-16 code units

    JsonValue toJson() const {
        std::map<std::string, JsonValue> obj;
        obj["line"] = line;
        obj["character"] = character;
        return JsonValue(obj);
    }

    static LspPosition fromJson(const JsonValue& j) {
        LspPosition p;
        p.line = j.get("line").asInt(0);
        p.character = j.get("character").asInt(0);
        return p;
    }

    static LspPosition fromSourcePosition(const SourcePosition& sp) {
        LspPosition lp;
        lp.line = utf::lineToLspLine(sp.line);
        lp.character = utf::columnToLspCharacter(sp.column);
        return lp;
    }
};

struct LspRange {
    LspPosition start;
    LspPosition end;

    JsonValue toJson() const {
        std::map<std::string, JsonValue> obj;
        obj["start"] = start.toJson();
        obj["end"] = end.toJson();
        return JsonValue(obj);
    }

    static LspRange fromJson(const JsonValue& j) {
        LspRange r;
        r.start = LspPosition::fromJson(j.get("start"));
        r.end = LspPosition::fromJson(j.get("end"));
        return r;
    }

    static LspRange fromSourceRange(const SourceRange& sr) {
        LspRange lr;
        lr.start = LspPosition::fromSourcePosition(sr.start);
        lr.end = LspPosition::fromSourcePosition(sr.end);
        return lr;
    }
};

struct LspDiagnostic {
    LspRange range;
    int severity = 1; // 1 = Error, 2 = Warning, 3 = Info, 4 = Hint
    std::string message;
    std::string source = "skrumi";
    int code = 0;

    JsonValue toJson() const {
        std::map<std::string, JsonValue> obj;
        obj["range"] = range.toJson();
        obj["severity"] = severity;
        obj["message"] = message;
        obj["source"] = source;
        if (code != 0) obj["code"] = "SKR" + std::to_string(code);
        return JsonValue(obj);
    }

    static LspDiagnostic fromDiagnostic(const Diagnostic& d) {
        LspDiagnostic ld;
        ld.range = LspRange::fromSourceRange(d.range);
        ld.severity = static_cast<int>(d.severity);
        ld.message = d.message;
        ld.source = d.source;
        ld.code = d.code;
        return ld;
    }
};

struct LspCompletionItem {
    std::string label;
    int kind = 1; // 1=Text, 3=Function, 6=Variable, 14=Keyword
    std::string detail;
    std::string documentation;

    JsonValue toJson() const {
        std::map<std::string, JsonValue> obj;
        obj["label"] = label;
        obj["kind"] = kind;
        if (!detail.empty()) obj["detail"] = detail;
        if (!documentation.empty()) {
            std::map<std::string, JsonValue> docObj;
            docObj["kind"] = "markdown";
            docObj["value"] = documentation;
            obj["documentation"] = JsonValue(docObj);
        }
        return JsonValue(obj);
    }
};

struct LspLocation {
    std::string uri;
    LspRange range;

    JsonValue toJson() const {
        std::map<std::string, JsonValue> obj;
        obj["uri"] = uri;
        obj["range"] = range.toJson();
        return JsonValue(obj);
    }
};

struct LspDocumentSymbol {
    std::string name;
    int kind = 12; // 12=Function, 13=Variable, 2=Module
    LspRange range;
    LspRange selectionRange;

    JsonValue toJson() const {
        std::map<std::string, JsonValue> obj;
        obj["name"] = name;
        obj["kind"] = kind;
        obj["range"] = range.toJson();
        obj["selectionRange"] = selectionRange.toJson();
        return JsonValue(obj);
    }
};

} // namespace lsp
} // namespace skrumi
