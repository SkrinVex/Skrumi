#pragma once
#include <string>
#include "skrumi/SourceLocation.h"

namespace skrumi {

// Only syntax has its own token. Builtin functions are ordinary identifiers, so a
// program calls print, size or read_file exactly like a function it defined itself.
enum class TokenType {
    NUMBER, STRING_LITERAL,
    // A string with ${expression} parts: "a ${x} b ${y} c" is STRING_BEGIN("a "), the
    // tokens of x, STRING_MIDDLE(" b "), the tokens of y, STRING_END(" c").
    STRING_BEGIN, STRING_MIDDLE, STRING_END,
    PLUS, MINUS, STAR, SLASH, MOD, INC, DEC,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    SEMICOLON, COMMA, ASSIGN, DOT, COLON,
    EQ, NEQ, LT, GT, LTE, GTE, AND, OR, NOT,
    PLUS_ASSIGN, MINUS_ASSIGN, STAR_ASSIGN, SLASH_ASSIGN, MOD_ASSIGN,
    BIT_AND, BIT_OR, BIT_XOR, BIT_NOT, BIT_AND_ASSIGN, BIT_OR_ASSIGN, BIT_XOR_ASSIGN,
    SHL_ASSIGN, SHR_ASSIGN,

    INT_KW, FLOAT_KW, STRING_KW, BOOL_KW, VOID_KW, ARRAY, MAP_KW, FUNC_KW,
    ARROW, // => of a lambda
    QUESTION,          // ? after a type: string? name
    QUESTION_QUESTION, // ?? : the left side unless it is null
    QUESTION_DOT,      // ?. : a field or method, or null when the value is null
    TRUE_KW, FALSE_KW, NULL_KW,
    WHILE, FOR, IF, ELSE, SWITCH, CASE, DEFAULT,
    INCLUDE, USING, RETURN, GLOBAL, BREAK, CONTINUE,
    STRUCT, TRY, CATCH, FINALLY, THROW, CONST_KW, ENUM,

    IDENTIFIER,
    END, ERROR
};

struct Token {
    TokenType type = TokenType::END;
    std::string value;
    int line = 1;
    int column = 1;
    SourceRange range;

    Token() = default;
    Token(TokenType t, std::string v, int l, int c, SourceRange r = {})
        : type(t), value(std::move(v)), line(l), column(c), range(r) {
        if (range.start.line == 0) {
            range.start.line = l;
            range.start.column = c;
            range.end.line = l;
            range.end.column = c + static_cast<int>(value.size());
        }
    }
};

const char* tokenTypeName(TokenType type);

// Keywords of the language, in the order editors list them.
const char* const* keywordList();

} // namespace skrumi

using skrumi::TokenType;
using skrumi::Token;
