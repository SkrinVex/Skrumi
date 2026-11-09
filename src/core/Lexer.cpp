#include "skrumi/Lexer.h"
#include <cctype>
#include <stdexcept>

namespace skrumi {

namespace {
struct Keyword { const char* text; TokenType type; };
constexpr Keyword keywords[] = {
    {"int", TokenType::INT_KW}, {"float", TokenType::FLOAT_KW}, {"string", TokenType::STRING_KW},
    {"bool", TokenType::BOOL_KW}, {"void", TokenType::VOID_KW}, {"array", TokenType::ARRAY},
    {"true", TokenType::TRUE_KW}, {"false", TokenType::FALSE_KW}, {"null", TokenType::NULL_KW},
    {"if", TokenType::IF}, {"else", TokenType::ELSE}, {"while", TokenType::WHILE}, {"for", TokenType::FOR},
    {"switch", TokenType::SWITCH}, {"case", TokenType::CASE}, {"default", TokenType::DEFAULT},
    {"break", TokenType::BREAK}, {"continue", TokenType::CONTINUE}, {"return", TokenType::RETURN},
    {"global", TokenType::GLOBAL}, {"include", TokenType::INCLUDE}, {"using", TokenType::USING},
    {"map", TokenType::MAP_KW}, {"func", TokenType::FUNC_KW}, {"struct", TokenType::STRUCT}, {"try", TokenType::TRY},
    {"catch", TokenType::CATCH}, {"finally", TokenType::FINALLY}, {"throw", TokenType::THROW}, {"const", TokenType::CONST_KW}, {"enum", TokenType::ENUM},
};
}

const char* const* keywordList() {
    static const char* const list[] = {
        "if", "else", "while", "for", "switch", "case", "default", "break", "continue", "return",
        "using", "include", "global", "int", "float", "string", "bool", "void", "true", "false", "null", "array",
        "map", "func", "struct", "try", "catch", "finally", "throw", "const", "enum", nullptr};
    return list;
}

const char* tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::NUMBER: return "number";
        case TokenType::STRING_LITERAL: return "string literal";
        case TokenType::STRING_BEGIN: return "string with ${";
        case TokenType::STRING_MIDDLE: case TokenType::STRING_END: return "the rest of a string after ${ }";
        case TokenType::PLUS: return "'+'";
        case TokenType::MINUS: return "'-'";
        case TokenType::STAR: return "'*'";
        case TokenType::SLASH: return "'/'";
        case TokenType::MOD: return "'%'";
        case TokenType::INC: return "'++'";
        case TokenType::DEC: return "'--'";
        case TokenType::LPAREN: return "'('";
        case TokenType::RPAREN: return "')'";
        case TokenType::LBRACE: return "'{'";
        case TokenType::RBRACE: return "'}'";
        case TokenType::LBRACKET: return "'['";
        case TokenType::RBRACKET: return "']'";
        case TokenType::SEMICOLON: return "';'";
        case TokenType::COMMA: return "','";
        case TokenType::ASSIGN: return "'='";
        case TokenType::DOT: return "'.'";
        case TokenType::COLON: return "':'";
        case TokenType::QUESTION: return "'?'";
        case TokenType::QUESTION_QUESTION: return "'??'";
        case TokenType::QUESTION_DOT: return "'?.'";
        case TokenType::EQ: return "'=='";
        case TokenType::NEQ: return "'!='";
        case TokenType::LT: return "'<'";
        case TokenType::GT: return "'>'";
        case TokenType::LTE: return "'<='";
        case TokenType::GTE: return "'>='";
        case TokenType::AND: return "'&&'";
        case TokenType::OR: return "'||'";
        case TokenType::NOT: return "'!'";
        case TokenType::PLUS_ASSIGN: return "'+='";
        case TokenType::MINUS_ASSIGN: return "'-='";
        case TokenType::STAR_ASSIGN: return "'*='";
        case TokenType::SLASH_ASSIGN: return "'/='";
        case TokenType::MOD_ASSIGN: return "'%='";
        case TokenType::BIT_AND: return "'&'";
        case TokenType::BIT_OR: return "'|'";
        case TokenType::BIT_XOR: return "'^'";
        case TokenType::BIT_NOT: return "'~'";
        case TokenType::BIT_AND_ASSIGN: return "'&='";
        case TokenType::BIT_OR_ASSIGN: return "'|='";
        case TokenType::BIT_XOR_ASSIGN: return "'^='";
        case TokenType::SHL_ASSIGN: return "'<<='";
        case TokenType::SHR_ASSIGN: return "'>>='";
        case TokenType::INT_KW: return "'int'";
        case TokenType::FLOAT_KW: return "'float'";
        case TokenType::STRING_KW: return "'string'";
        case TokenType::BOOL_KW: return "'bool'";
        case TokenType::VOID_KW: return "'void'";
        case TokenType::ARRAY: return "'array'";
        case TokenType::TRUE_KW: return "'true'";
        case TokenType::NULL_KW: return "'null'";
        case TokenType::FALSE_KW: return "'false'";
        case TokenType::WHILE: return "'while'";
        case TokenType::FOR: return "'for'";
        case TokenType::IF: return "'if'";
        case TokenType::ELSE: return "'else'";
        case TokenType::SWITCH: return "'switch'";
        case TokenType::CASE: return "'case'";
        case TokenType::DEFAULT: return "'default'";
        case TokenType::INCLUDE: return "'include'";
        case TokenType::USING: return "'using'";
        case TokenType::RETURN: return "'return'";
        case TokenType::GLOBAL: return "'global'";
        case TokenType::BREAK: return "'break'";
        case TokenType::CONTINUE: return "'continue'";
        case TokenType::MAP_KW: return "'map'";
        case TokenType::FUNC_KW: return "'func'";
        case TokenType::ARROW: return "'=>'";
        case TokenType::STRUCT: return "'struct'";
        case TokenType::TRY: return "'try'";
        case TokenType::CATCH: return "'catch'";
        case TokenType::FINALLY: return "'finally'";
        case TokenType::THROW: return "'throw'";
        case TokenType::CONST_KW: return "'const'";
        case TokenType::ENUM: return "'enum'";
        case TokenType::IDENTIFIER: return "identifier";
        case TokenType::END: return "end of file";
        case TokenType::ERROR: return "invalid character";
    }
    return "token";
}

namespace {
void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}
}

// \uXXXX (exactly four hex digits) or \u{X...} (one to six); the 'u' is already consumed.
std::string Lexer::unicodeEscape(SourcePosition literalStart) {
    bool braced = pos < source.size() && source[pos] == '{';
    if (braced) advanceChar();
    unsigned cp = 0;
    int digits = 0;
    while (pos < source.size() && std::isxdigit(static_cast<unsigned char>(source[pos])) && digits < (braced ? 6 : 4)) {
        char ch = source[pos];
        cp = cp * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(ch)) ? ch - '0' : (std::tolower(ch) - 'a' + 10));
        ++digits;
        advanceChar();
    }
    bool closed = !braced || (pos < source.size() && source[pos] == '}');
    if (braced && closed) advanceChar();
    // A UTF-16 surrogate pair written as two escapes, as JSON and Java write emoji.
    if (!braced && digits == 4 && cp >= 0xD800 && cp <= 0xDBFF && pos + 5 < source.size() &&
        source[pos] == '\\' && source[pos + 1] == 'u') {
        unsigned low = 0;
        bool hex = true;
        for (size_t i = 2; i < 6; ++i) {
            char ch = source[pos + i];
            if (!std::isxdigit(static_cast<unsigned char>(ch))) { hex = false; break; }
            low = low * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(ch)) ? ch - '0' : (std::tolower(ch) - 'a' + 10));
        }
        if (hex && low >= 0xDC00 && low <= 0xDFFF) {
            for (int i = 0; i < 6; ++i) advanceChar();
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        }
    }
    bool valid = closed && digits > 0 && (braced || digits == 4) && cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF);
    if (!valid) {
        std::string msg = "Invalid \\u escape in string literal";
        if (!collectDiagnostics) throw SyntaxError("Syntax Error: " + msg, literalStart.line);
        diagnostics.push_back({DiagnosticSeverity::Error, msg, {literalStart, currentPosition()}});
        return "";
    }
    std::string out;
    appendUtf8(out, cp);
    return out;
}

Lexer::Lexer(std::string src, bool collectDiags)
    : source(std::move(src)), collectDiagnostics(collectDiags) {}

SourcePosition Lexer::currentPosition() const {
    return {line, column, pos};
}

void Lexer::advanceChar() {
    if (pos >= source.size()) return;
    char current = source[pos];
    if (current == '\n') {
        line++;
        column = 1;
        pos++;
        return;
    }
    unsigned char b = static_cast<unsigned char>(current);
    if (b < 0x80) {
        column += 1;
        pos += 1;
    } else if ((b & 0xE0) == 0xC0 && pos + 1 < source.size()) {
        column += 1;
        pos += 2;
    } else if ((b & 0xF0) == 0xE0 && pos + 2 < source.size()) {
        column += 1;
        pos += 3;
    } else if ((b & 0xF8) == 0xF0 && pos + 3 < source.size()) {
        column += 2; // 4-byte UTF-8 emoji corresponds to 2 UTF-16 code units
        pos += 4;
    } else {
        column += 1;
        pos += 1;
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    diagnostics.clear();
    // A BOM is an encoding signature only at the start; preserve byte offsets.
    if (pos == 0 && source.compare(0, 3, "\xEF\xBB\xBF") == 0) pos = 3;

    while (pos < source.length()) {
        char current = source[pos];

        if (std::isspace(static_cast<unsigned char>(current))) {
            advanceChar();
            continue;
        }

        // Single-line comments: //
        if (current == '/' && pos + 1 < source.length() && source[pos + 1] == '/') {
            while (pos < source.length() && source[pos] != '\n') {
                advanceChar();
            }
            continue;
        }

        SourcePosition startPos = currentPosition();
        if (current == '/' && pos + 1 < source.size() && source[pos + 1] == '*') {
            advanceChar(); advanceChar();
            while (pos < source.size() && !(source[pos] == '*' && pos + 1 < source.size() && source[pos + 1] == '/'))
                advanceChar();
            if (pos == source.size()) {
                const std::string message = "Unterminated block comment; expected '*/'";
                if (!collectDiagnostics) throw SyntaxError("Syntax Error: " + message, startPos.line);
                diagnostics.push_back({DiagnosticSeverity::Error, message, {startPos, currentPosition()}});
            } else { advanceChar(); advanceChar(); }
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(current))) {
            std::string num;
            bool valid = true;
            int base = 10;
            if (current == '0' && pos + 1 < source.size()) {
                char prefix = source[pos + 1];
                if (prefix == 'x' || prefix == 'X' || prefix == 'b' || prefix == 'B') {
                    base = prefix == 'x' || prefix == 'X' ? 16 : 2;
                    num += source[pos]; advanceChar();
                    num += source[pos]; advanceChar();
                }
            }
            auto digit = [&](char c) {
                return base == 16 ? std::isxdigit(static_cast<unsigned char>(c)) != 0 :
                       base == 2 ? c == '0' || c == '1' : std::isdigit(static_cast<unsigned char>(c)) != 0;
            };
            auto digits = [&] {
                bool previousDigit = false, any = false;
                while (pos < source.size() && (digit(source[pos]) || source[pos] == '_')) {
                    char c = source[pos];
                    if (c == '_') {
                        if (!previousDigit || pos + 1 >= source.size() || !digit(source[pos + 1])) valid = false;
                    } else any = true;
                    previousDigit = c != '_';
                    num += c; advanceChar();
                }
                if (!any) valid = false;
            };
            digits();
            if (base == 10 && pos + 1 < source.size() && source[pos] == '.' &&
                std::isdigit(static_cast<unsigned char>(source[pos + 1]))) {
                num += '.'; advanceChar(); digits();
            }
            if (base == 10 && pos < source.size() && (source[pos] == 'e' || source[pos] == 'E')) {
                num += source[pos]; advanceChar();
                if (pos < source.size() && (source[pos] == '+' || source[pos] == '-')) {
                    num += source[pos]; advanceChar();
                }
                digits();
            }
            if (pos < source.size() && (std::isalnum(static_cast<unsigned char>(source[pos])) || source[pos] == '_')) {
                valid = false;
                while (pos < source.size() && (std::isalnum(static_cast<unsigned char>(source[pos])) || source[pos] == '_')) {
                    num += source[pos]; advanceChar();
                }
            }
            SourcePosition endPos = currentPosition();
            if (!valid) {
                std::string message = std::string("Invalid ") + (base == 16 ? "hexadecimal" : base == 2 ? "binary" : "numeric") + " literal '" + num + "'; check base digits, exponent and separators";
                if (!collectDiagnostics) throw SyntaxError("Syntax Error: " + message, startPos.line);
                diagnostics.push_back({DiagnosticSeverity::Error, message, {startPos, endPos}});
            }
            tokens.push_back({valid ? TokenType::NUMBER : TokenType::ERROR, num, startPos.line, startPos.column, {startPos, endPos}});
        }
        else if (current == '"') {
            advanceChar(); // consume opening quote
            scanString(tokens, startPos, false);
        } 
        else if (std::isalpha(static_cast<unsigned char>(current)) || current == '_') {
            std::string id;
            while (pos < source.length() && (std::isalnum(static_cast<unsigned char>(source[pos])) || source[pos] == '_')) {
                id += source[pos];
                advanceChar();
            }
            SourcePosition endPos = currentPosition();
            SourceRange range{startPos, endPos};

            TokenType type = TokenType::IDENTIFIER;
            for (const auto& keyword : keywords) {
                if (id == keyword.text) { type = keyword.type; break; }
            }

            tokens.push_back({type, id, startPos.line, startPos.column, range});
        } 
        else {
            if ((current == '<' || current == '>') && pos + 2 < source.size() &&
                source[pos + 1] == current && source[pos + 2] == '=') {
                std::string op = source.substr(pos, 3);
                advanceChar(); advanceChar(); advanceChar();
                tokens.push_back({current == '<' ? TokenType::SHL_ASSIGN : TokenType::SHR_ASSIGN,
                                  op, startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if ((current == '&' || current == '|' || current == '^') && pos + 1 < source.size() && source[pos + 1] == '=') {
                std::string op = source.substr(pos, 2);
                advanceChar(); advanceChar();
                TokenType type = current == '&' ? TokenType::BIT_AND_ASSIGN : current == '|' ? TokenType::BIT_OR_ASSIGN : TokenType::BIT_XOR_ASSIGN;
                tokens.push_back({type, op, startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            // Check 2-character operators
            if (current == '=' && pos + 1 < source.length() && source[pos + 1] == '>') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::ARROW, "=>", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '=' && pos + 1 < source.length() && source[pos + 1] == '=') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::EQ, "==", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '!' && pos + 1 < source.length() && source[pos + 1] == '=') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::NEQ, "!=", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '<' && pos + 1 < source.length() && source[pos + 1] == '=') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::LTE, "<=", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '>' && pos + 1 < source.length() && source[pos + 1] == '=') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::GTE, ">=", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '+' && pos + 1 < source.length() && source[pos + 1] == '+') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::INC, "++", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '+' && pos + 1 < source.length() && source[pos + 1] == '=') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::PLUS_ASSIGN, "+=", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '-' && pos + 1 < source.length() && source[pos + 1] == '-') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::DEC, "--", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '%' && pos + 1 < source.length() && source[pos + 1] == '=') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::MOD_ASSIGN, "%=", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '-' && pos + 1 < source.length() && source[pos + 1] == '=') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::MINUS_ASSIGN, "-=", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '*' && pos + 1 < source.length() && source[pos + 1] == '=') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::STAR_ASSIGN, "*=", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '/' && pos + 1 < source.length() && source[pos + 1] == '=') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::SLASH_ASSIGN, "/=", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '&' && pos + 1 < source.length() && source[pos + 1] == '&') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::AND, "&&", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '?' && pos + 1 < source.length() && (source[pos + 1] == '?' || source[pos + 1] == '.')) {
                bool coalesce = source[pos + 1] == '?';
                advanceChar(); advanceChar();
                tokens.push_back({coalesce ? TokenType::QUESTION_QUESTION : TokenType::QUESTION_DOT, coalesce ? "??" : "?.",
                                  startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }
            if (current == '|' && pos + 1 < source.length() && source[pos + 1] == '|') {
                advanceChar(); advanceChar();
                tokens.push_back({TokenType::OR, "||", startPos.line, startPos.column, {startPos, currentPosition()}});
                continue;
            }

            TokenType singleType = TokenType::ERROR;
            std::string singleVal(1, current);
            switch (current) {
                case '&': singleType = TokenType::BIT_AND; break;
                case '|': singleType = TokenType::BIT_OR; break;
                case '^': singleType = TokenType::BIT_XOR; break;
                case '~': singleType = TokenType::BIT_NOT; break;
                case '+': singleType = TokenType::PLUS; break;
                case '-': singleType = TokenType::MINUS; break;
                case '*': singleType = TokenType::STAR; break;
                case '/': singleType = TokenType::SLASH; break;
                case '%': singleType = TokenType::MOD; break;
                case '(': singleType = TokenType::LPAREN; break;
                case ')': singleType = TokenType::RPAREN; break;
                case '{':
                    singleType = TokenType::LBRACE;
                    if (!interpolations.empty()) ++interpolations.back();
                    break;
                case '}':
                    if (!interpolations.empty() && interpolations.back() == 0) {
                        // The end of a ${ } expression: the string goes on.
                        interpolations.pop_back();
                        advanceChar();
                        scanString(tokens, startPos, true);
                        continue;
                    }
                    if (!interpolations.empty()) --interpolations.back();
                    singleType = TokenType::RBRACE;
                    break;
                case '[': singleType = TokenType::LBRACKET; break;
                case ']': singleType = TokenType::RBRACKET; break;
                case ';': singleType = TokenType::SEMICOLON; break;
                case ',': singleType = TokenType::COMMA; break;
                case '=': singleType = TokenType::ASSIGN; break;
                case '.': singleType = TokenType::DOT; break;
                case '!': singleType = TokenType::NOT; break;
                case '<': singleType = TokenType::LT; break;
                case '>': singleType = TokenType::GT; break;
                case ':': singleType = TokenType::COLON; break;
                case '?': singleType = TokenType::QUESTION; break;
                default: 
                    advanceChar();
                    SourcePosition badEnd = currentPosition();
                    std::string err = std::string("Unknown character '") + current + "'";
                    if (collectDiagnostics) {
                        diagnostics.push_back({DiagnosticSeverity::Error, err, {startPos, badEnd}});
                        tokens.push_back({TokenType::ERROR, singleVal, startPos.line, startPos.column, {startPos, badEnd}});
                        continue;
                    } else {
                        throw SyntaxError(std::string("Syntax Error: Unknown character '") + current + "'", startPos.line);
                    }
            }
            advanceChar();
            tokens.push_back({singleType, singleVal, startPos.line, startPos.column, {startPos, currentPosition()}});
        }
    }

    SourcePosition eofPos = currentPosition();
    if (!interpolations.empty()) {
        interpolations.clear();
        std::string msg = "Unclosed ${ in a string: expected '}'";
        if (!collectDiagnostics) throw SyntaxError("Syntax Error: " + msg, eofPos.line);
        diagnostics.push_back({DiagnosticSeverity::Error, msg, {eofPos, eofPos}});
    }
    tokens.push_back({TokenType::END, "", eofPos.line, eofPos.column, {eofPos, eofPos}});
    return tokens;
}

void Lexer::scanString(std::vector<Token>& tokens, SourcePosition startPos, bool resumed) {
    std::string str;
    bool closed = false, interpolation = false;
    while (pos < source.length()) {
        if (source[pos] == '"') {
            closed = true;
            advanceChar(); // consume closing quote
            break;
        }
        if (source[pos] == '$' && pos + 1 < source.length() && source[pos + 1] == '{') {
            interpolation = true;
            advanceChar();
            advanceChar();
            break;
        }
        if (source[pos] == '\\' && pos + 1 < source.length()) {
            advanceChar(); // consume backslash
            char escaped = source[pos];
            if (escaped == 'u') {
                advanceChar();
                str += unicodeEscape(startPos);
                continue;
            }
            switch (escaped) {
                case 'n': str += '\n'; break;
                case 't': str += '\t'; break;
                case 'r': str += '\r'; break;
                case '0': str += '\0'; break;
                case '\\': str += '\\'; break;
                case '"': str += '"'; break;
                default: str += escaped; break; // \$ is a dollar sign that starts no ${
            }
            advanceChar();
        } else {
            size_t curPos = pos;
            advanceChar();
            str.append(source.data() + curPos, pos - curPos);
        }
    }
    SourcePosition endPos = currentPosition();
    if (!closed && !interpolation) {
        std::string msg = "Unclosed string literal";
        if (collectDiagnostics) {
            diagnostics.push_back({DiagnosticSeverity::Error, msg, {startPos, endPos}});
        } else {
            throw SyntaxError("Syntax Error: " + msg, startPos.line);
        }
    }
    TokenType type = interpolation ? (resumed ? TokenType::STRING_MIDDLE : TokenType::STRING_BEGIN)
                                   : (resumed ? TokenType::STRING_END : TokenType::STRING_LITERAL);
    if (interpolation) interpolations.push_back(0);
    tokens.push_back({type, str, startPos.line, startPos.column, {startPos, endPos}});
}

} // namespace skrumi
