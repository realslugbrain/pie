#include "pie/core/token/token.h"

#include <stdlib.h>

const char *pie_token_kind_name(PieTokenKind kind) {
  switch (kind) {
  case PIE_TOK_EOF:
    return "end of file";
  case PIE_TOK_NEWLINE:
    return "newline";
  case PIE_TOK_IDENTIFIER:
    return "identifier";
  case PIE_TOK_INT_LITERAL:
    return "integer literal";
  case PIE_TOK_FLOAT_LITERAL:
    return "float literal";
  case PIE_TOK_CHAR_LITERAL:
    return "char literal";
  case PIE_TOK_STRING_LITERAL:
    return "string literal";
  case PIE_TOK_STRING_OPEN:
    return "string interpolation open";
  case PIE_TOK_STRING_CLOSE:
    return "string interpolation close";
  case PIE_TOK_TRUE:
    return "true";
  case PIE_TOK_FALSE:
    return "false";
  case PIE_TOK_NULL:
    return "null";
  case PIE_TOK_AND:
    return "and";
  case PIE_TOK_OR:
    return "or";
  case PIE_TOK_NOT:
    return "not";
  case PIE_TOK_LPAREN:
    return "(";
  case PIE_TOK_RPAREN:
    return ")";
  case PIE_TOK_COMMA:
    return ",";
  case PIE_TOK_COLON:
    return ":";
  case PIE_TOK_COLON_COLON:
    return "::";
  case PIE_TOK_ARROW:
    return "->";
  case PIE_TOK_PLUS:
    return "+";
  case PIE_TOK_MINUS:
    return "-";
  case PIE_TOK_STAR:
    return "*";
  case PIE_TOK_SLASH:
    return "/";
  case PIE_TOK_PERCENT:
    return "%";
  case PIE_TOK_AMP:
    return "&";
  case PIE_TOK_EQ:
    return "=";
  case PIE_TOK_EQ_EQ:
    return "==";
  case PIE_TOK_BANG:
    return "!";
  case PIE_TOK_BANG_EQ:
    return "!=";
  case PIE_TOK_LT:
    return "<";
  case PIE_TOK_LT_EQ:
    return "<=";
  case PIE_TOK_GT:
    return ">";
  case PIE_TOK_GT_EQ:
    return ">=";
  case PIE_TOK_PLUS_EQ:
    return "+=";
  case PIE_TOK_MINUS_EQ:
    return "-=";
  case PIE_TOK_STAR_EQ:
    return "*=";
  case PIE_TOK_SLASH_EQ:
    return "/=";
  case PIE_TOK_PERCENT_EQ:
    return "%=";
  case PIE_TOK_PLUS_PLUS:
    return "++";
  case PIE_TOK_STAR_STAR:
    return "**";
  case PIE_TOK_STAR_STAR_EQ:
    return "**=";
  case PIE_TOK_MINUS_MINUS:
    return "--";
  case PIE_TOK_DOT_DOT:
    return "..";
  case PIE_TOK_DOT_DOT_EQ:
    return "..=";
  case PIE_TOK_QUESTION:
    return "?";
  case PIE_TOK_LBRACKET:
    return "[";
  case PIE_TOK_RBRACKET:
    return "]";
  case PIE_TOK_LBRACE:
    return "{";
  case PIE_TOK_RBRACE:
    return "}";
  case PIE_TOK_PIPE:
    return "|";
  case PIE_TOK_CARET:
    return "^";
  case PIE_TOK_TILDE:
    return "~";
  case PIE_TOK_LT_LT:
    return "<<";
  case PIE_TOK_GT_GT:
    return ">>";
  case PIE_TOK_FN:
    return "fn";
  case PIE_TOK_PUB:
    return "pub";
  case PIE_TOK_RETURN:
    return "return";
  case PIE_TOK_BREAK:
    return "break";
  case PIE_TOK_CONTINUE:
    return "continue";
  case PIE_TOK_FOR:
    return "for";
  case PIE_TOK_IN:
    return "in";
  case PIE_TOK_END:
    return "end";
  case PIE_TOK_IF:
    return "if";
  case PIE_TOK_ELIF:
    return "elif";
  case PIE_TOK_ELSE:
    return "else";
  case PIE_TOK_WHILE:
    return "while";
  case PIE_TOK_REGION:
    return "region";
  case PIE_TOK_UNSAFE:
    return "unsafe";
  case PIE_TOK_RAW:
    return "raw";
  case PIE_TOK_REQUIRE:
    return "require";
  case PIE_TOK_PACKAGE:
    return "package";
  case PIE_TOK_IMPORT:
    return "import";
  case PIE_TOK_FROM:
    return "from";
  case PIE_TOK_LET:
    return "let";
  case PIE_TOK_MUT:
    return "mut";
  case PIE_TOK_CONST:
    return "const";
  case PIE_TOK_PRINT:
    return "print";
  case PIE_TOK_PRINTLN:
    return "println";
  case PIE_TOK_STRUCT:
    return "struct";
  case PIE_TOK_NEW:
    return "new";
  case PIE_TOK_ENUM:
    return "enum";
  case PIE_TOK_CASE:
    return "case";
  case PIE_TOK_MATCH:
    return "match";
  case PIE_TOK_AS:
    return "as";
  case PIE_TOK_DOT:
    return ".";
  case PIE_TOK_INT_TYPE:
    return "int";
  case PIE_TOK_FLOAT_TYPE:
    return "float";
  case PIE_TOK_CHAR_TYPE:
    return "char";
  case PIE_TOK_BYTE_TYPE:
    return "byte";
  case PIE_TOK_STRING_TYPE:
    return "string";
  case PIE_TOK_BOOL_TYPE:
    return "bool";
  case PIE_TOK_VOID_TYPE:
    return "void";
  case PIE_TOK_LIST_TYPE:
    return "list";
  case PIE_TOK_MAP_TYPE:
    return "map";
  case PIE_TOK_PASS:
    return "pass";
  case PIE_TOK_AUTO:
    return "auto";
  case PIE_TOK_DEFER:
    return "defer";
  case PIE_TOK_WHERE:
    return "where";
  case PIE_TOK_TRY:
    return "try";
  case PIE_TOK_SELF:
    return "self";
  }
  return "unknown token";
}

void pie_token_list_init(PieTokenList *tokens) {
  tokens->items = NULL;
  tokens->count = 0;
  tokens->capacity = 0;
}

void pie_token_list_free(PieTokenList *tokens) {
  for (size_t i = 0; i < tokens->count; i++) {
    free(tokens->items[i].string_value);
  }
  free(tokens->items);
  pie_token_list_init(tokens);
}

int pie_token_list_push(PieTokenList *tokens, PieToken token) {
  if (tokens->count == tokens->capacity) {
    size_t next_capacity = tokens->capacity ? tokens->capacity * 2 : 128;
    PieToken *next =
        (PieToken *)realloc(tokens->items, next_capacity * sizeof(PieToken));
    if (!next) {
      return 0;
    }
    tokens->items = next;
    tokens->capacity = next_capacity;
  }
  tokens->items[tokens->count++] = token;
  return 1;
}
