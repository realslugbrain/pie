#include "pie/core/lexer/lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct Lexer {
  const PieSource *source;
  const char *p;
  int line;
  int column;
  PieTokenList *tokens;
  PieDiagnosticBag *diag;
} Lexer;

static int is_ident_start(int c) { return isalpha(c) || c == '_'; }

static int is_ident_continue(int c) { return isalnum(c) || c == '_'; }

static char peek(Lexer *lexer) { return *lexer->p; }

static char peek_next(Lexer *lexer) { return lexer->p[0] ? lexer->p[1] : '\0'; }

static char advance(Lexer *lexer) {
  char c = *lexer->p++;
  if (c == '\n') {
    lexer->line++;
    lexer->column = 1;
  } else {
    lexer->column++;
  }
  return c;
}

static int push_token(Lexer *lexer, PieToken token) {
  if (!pie_token_list_push(lexer->tokens, token)) {
    pie_diag_error(lexer->diag, "out of memory while lexing");
    return 0;
  }
  return 1;
}

static PieToken make_token(Lexer *lexer, PieTokenKind kind, const char *start,
                           int line, int column) {
  PieToken token;
  memset(&token, 0, sizeof(token));
  token.kind = kind;
  token.start = start;
  token.len = (size_t)(lexer->p - start);
  token.line = line;
  token.column = column;
  return token;
}

static PieTokenKind keyword_kind(const char *start, size_t len) {
  struct Keyword {
    const char *text;
    PieTokenKind kind;
  };
  static const struct Keyword keywords[] = {
      {"and", PIE_TOK_AND},
      {"auto", PIE_TOK_AUTO},
      {"bool", PIE_TOK_BOOL_TYPE},
      {"break", PIE_TOK_BREAK},
      {"continue", PIE_TOK_CONTINUE},
      {"defer", PIE_TOK_DEFER},
      {"where", PIE_TOK_WHERE},
      {"byte", PIE_TOK_BYTE_TYPE},
      {"case", PIE_TOK_CASE},
      {"char", PIE_TOK_CHAR_TYPE},
      {"const", PIE_TOK_CONST},
      {"end", PIE_TOK_END},
      {"elif", PIE_TOK_ELIF},
      {"else", PIE_TOK_ELSE},
      {"enum", PIE_TOK_ENUM},
      {"export", PIE_TOK_EXPORT},
      {"float", PIE_TOK_FLOAT_TYPE},
      {"fn", PIE_TOK_FN},
      {"for", PIE_TOK_FOR},
      {"from", PIE_TOK_FROM},
      {"if", PIE_TOK_IF},
      {"import", PIE_TOK_IMPORT},
      {"in", PIE_TOK_IN},
      {"int", PIE_TOK_INT_TYPE},
      {"let", PIE_TOK_LET},
      {"list", PIE_TOK_LIST_TYPE},
      {"map", PIE_TOK_MAP_TYPE},
      {"match", PIE_TOK_MATCH},
      {"as", PIE_TOK_AS},
      {"mut", PIE_TOK_MUT},
      {"new", PIE_TOK_NEW},
      {"not", PIE_TOK_NOT},
      {"or", PIE_TOK_OR},
      {"package", PIE_TOK_PACKAGE},
      {"pass", PIE_TOK_PASS},
      {"print", PIE_TOK_PRINT},
      {"println", PIE_TOK_PRINTLN},
      {"pub", PIE_TOK_PUB},
      {"raw", PIE_TOK_RAW},
      {"region", PIE_TOK_REGION},
      {"require", PIE_TOK_REQUIRE},
      {"return", PIE_TOK_RETURN},
      {"self", PIE_TOK_SELF},
      {"string", PIE_TOK_STRING_TYPE},
      {"struct", PIE_TOK_STRUCT},
      {"try", PIE_TOK_TRY},
      {"type", PIE_TOK_TYPE},
      {"assert", PIE_TOK_ASSERT},
      {"assert_eq", PIE_TOK_ASSERT_EQ},
      {"do", PIE_TOK_DO},
      {"format", PIE_TOK_FORMAT},
      {"false", PIE_TOK_FALSE},
      {"true", PIE_TOK_TRUE},
      {"null", PIE_TOK_NULL},
      {"unsafe", PIE_TOK_UNSAFE},
      {"void", PIE_TOK_VOID_TYPE},
      {"while", PIE_TOK_WHILE},
  };

  for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
    if (strlen(keywords[i].text) == len &&
        memcmp(start, keywords[i].text, len) == 0) {
      return keywords[i].kind;
    }
  }
  return PIE_TOK_IDENTIFIER;
}

static int lex_identifier(Lexer *lexer) {
  const char *start = lexer->p;
  int line = lexer->line;
  int column = lexer->column;
  advance(lexer);
  while (is_ident_continue((unsigned char)peek(lexer))) {
    advance(lexer);
  }
  PieToken token =
      make_token(lexer, keyword_kind(start, (size_t)(lexer->p - start)), start,
                 line, column);
  return push_token(lexer, token);
}

static int lex_number(Lexer *lexer) {
  const char *start = lexer->p;
  int line = lexer->line;
  int column = lexer->column;
  int is_float = 0;

  if (peek(lexer) == '0' &&
      (peek_next(lexer) == 'x' || peek_next(lexer) == 'X')) {
    advance(lexer);
    advance(lexer);
    while (isxdigit((unsigned char)peek(lexer))) {
      advance(lexer);
    }
  } else if (peek(lexer) == '0' &&
             (peek_next(lexer) == 'b' || peek_next(lexer) == 'B')) {
    advance(lexer);
    advance(lexer);
    while (peek(lexer) == '0' || peek(lexer) == '1') {
      advance(lexer);
    }
  } else if (peek(lexer) == '0' &&
             (peek_next(lexer) == 'o' || peek_next(lexer) == 'O')) {
    advance(lexer);
    advance(lexer);
    while (peek(lexer) >= '0' && peek(lexer) <= '7') {
      advance(lexer);
    }
  } else {
    while (isdigit((unsigned char)peek(lexer))) {
      advance(lexer);
    }
    if (peek(lexer) == '.' && isdigit((unsigned char)peek_next(lexer))) {
      is_float = 1;
      advance(lexer);
      while (isdigit((unsigned char)peek(lexer))) {
        advance(lexer);
      }
    }
    if (peek(lexer) == 'e' || peek(lexer) == 'E') {
      const char *exp_start = lexer->p;
      int exp_line = lexer->line;
      int exp_column = lexer->column;
      is_float = 1;
      advance(lexer);
      if (peek(lexer) == '+' || peek(lexer) == '-') {
        advance(lexer);
      }
      if (!isdigit((unsigned char)peek(lexer))) {
        pie_diag_errorf(lexer->diag,
                        "%d:%d: expected digits after float exponent", exp_line,
                        exp_column);
        (void)exp_start;
        return 0;
      }
      while (isdigit((unsigned char)peek(lexer))) {
        advance(lexer);
      }
    }
  }

  PieToken token =
      make_token(lexer, is_float ? PIE_TOK_FLOAT_LITERAL : PIE_TOK_INT_LITERAL,
                 start, line, column);
  char *end = NULL;
  if (is_float) {
    token.float_value = strtod(start, &end);
  } else if (token.len > 2 && start[0] == '0' &&
             (start[1] == 'b' || start[1] == 'B')) {
    long long value = 0;
    for (size_t i = 2; i < token.len; i++) {
      value = value * 2 + (start[i] - '0');
    }
    token.int_value = value;
  } else if (token.len > 2 && start[0] == '0' &&
             (start[1] == 'o' || start[1] == 'O')) {
    long long value = 0;
    for (size_t i = 2; i < token.len; i++) {
      value = value * 8 + (start[i] - '0');
    }
    token.int_value = value;
  } else {
    token.int_value = strtoll(start, &end, 0);
  }
  return push_token(lexer, token);
}

static int parse_char_escape(Lexer *lexer, unsigned int *out_value) {
  char escaped = advance(lexer);
  switch (escaped) {
  case 'n':
    *out_value = '\n';
    return 1;
  case 't':
    *out_value = '\t';
    return 1;
  case 'r':
    *out_value = '\r';
    return 1;
  case '\\':
    *out_value = '\\';
    return 1;
  case '\'':
    *out_value = '\'';
    return 1;
  case '0':
    *out_value = '\0';
    return 1;
  default:
    pie_diag_errorf(lexer->diag,
                    "%d:%d: unsupported char escape sequence '\\%c'",
                    lexer->line, lexer->column, escaped);
    return 0;
  }
}

static int lex_char(Lexer *lexer) {
  const char *start = lexer->p;
  int line = lexer->line;
  int column = lexer->column;
  unsigned int value = 0;

  advance(lexer);
  if (!peek(lexer) || peek(lexer) == '\n') {
    pie_diag_errorf(lexer->diag, "%d:%d: unterminated char literal", line,
                    column);
    return 0;
  }
  if (peek(lexer) == '\\') {
    advance(lexer);
    if (!parse_char_escape(lexer, &value)) {
      return 0;
    }
  } else {
    value = (unsigned char)advance(lexer);
  }

  if (peek(lexer) != '\'') {
    pie_diag_errorf(lexer->diag,
                    "%d:%d: char literal must contain exactly one character",
                    line, column);
    return 0;
  }
  advance(lexer);

  PieToken token = make_token(lexer, PIE_TOK_CHAR_LITERAL, start, line, column);
  token.int_value = (long long)value;
  return push_token(lexer, token);
}

static int append_char(char **buf, size_t *count, size_t *capacity, char c) {
  if (*count == *capacity) {
    size_t next_capacity = *capacity ? *capacity * 2 : 16;
    char *next = (char *)realloc(*buf, next_capacity);
    if (!next) {
      return 0;
    }
    *buf = next;
    *capacity = next_capacity;
  }
  (*buf)[(*count)++] = c;
  return 1;
}

static int lex_string(Lexer *lexer) {
  const char *start = lexer->p;
  int line = lexer->line;
  int column = lexer->column;
  char *buf = NULL;
  size_t count = 0;
  size_t capacity = 0;

  advance(lexer);
  while (peek(lexer) && peek(lexer) != '"') {
    char c = advance(lexer);
    if (c == '\n') {
      free(buf);
      pie_diag_errorf(lexer->diag, "%d:%d: unterminated string literal", line,
                      column);
      return 0;
    }
    if (c == '\\') {
      char next = peek(lexer);
      if (next == '(') {
        advance(lexer);
        PieToken token =
            make_token(lexer, PIE_TOK_STRING_LITERAL, start, line, column);
        token.string_value = buf;
        token.string_len = count;
        if (!push_token(lexer, token)) {
          free(buf);
          return 0;
        }
        PieToken open =
            make_token(lexer, PIE_TOK_STRING_OPEN, lexer->p - 1, line, column);
        open.string_value = NULL;
        open.string_len = 0;
        if (!push_token(lexer, open)) {
          return 0;
        }
        int depth = 1;
        while (peek(lexer) && depth > 0) {
          char ch = advance(lexer);
          if (ch == '(')
            depth++;
          else if (ch == ')')
            depth--;
        }
        if (depth != 0) {
          pie_diag_errorf(lexer->diag,
                          "%d:%d: unterminated string interpolation", line,
                          column);
          return 0;
        }
        PieToken close =
            make_token(lexer, PIE_TOK_STRING_CLOSE, lexer->p - 1, line, column);
        close.string_value = NULL;
        close.string_len = 0;
        if (!push_token(lexer, close)) {
          return 0;
        }
        start = lexer->p;
        line = lexer->line;
        column = lexer->column;
        buf = NULL;
        count = 0;
        capacity = 0;
        continue;
      }
      char escaped = advance(lexer);
      switch (escaped) {
      case 'n':
        c = '\n';
        break;
      case 't':
        c = '\t';
        break;
      case 'r':
        c = '\r';
        break;
      case '\\':
        c = '\\';
        break;
      case '"':
        c = '"';
        break;
      default:
        free(buf);
        pie_diag_errorf(lexer->diag,
                        "%d:%d: unsupported escape sequence '\\%c'",
                        lexer->line, lexer->column, escaped);
        return 0;
      }
    }
    if (!append_char(&buf, &count, &capacity, c)) {
      free(buf);
      pie_diag_error(lexer->diag, "out of memory while lexing string literal");
      return 0;
    }
  }

  if (peek(lexer) != '"') {
    free(buf);
    pie_diag_errorf(lexer->diag, "%d:%d: unterminated string literal", line,
                    column);
    return 0;
  }
  advance(lexer);

  PieToken token =
      make_token(lexer, PIE_TOK_STRING_LITERAL, start, line, column);
  token.string_value = buf;
  token.string_len = count;
  return push_token(lexer, token);
}

static void skip_block_comment(Lexer *lexer) {
  advance(lexer);
  advance(lexer);
  while (peek(lexer)) {
    if (peek(lexer) == '*' && peek_next(lexer) == '/') {
      advance(lexer);
      advance(lexer);
      return;
    }
    advance(lexer);
  }
  pie_diag_error(lexer->diag, "unterminated block comment");
}

static int lex_one(Lexer *lexer) {
  char c = peek(lexer);
  const char *start = lexer->p;
  int line = lexer->line;
  int column = lexer->column;

  if (c == ' ' || c == '\t' || c == '\r') {
    advance(lexer);
    return 1;
  }
  if (c == '\n') {
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_NEWLINE, start, line, column));
  }
  if (c == '#') {
    while (peek(lexer) && peek(lexer) != '\n') {
      advance(lexer);
    }
    return 1;
  }
  if (c == '/' && peek_next(lexer) == '*') {
    skip_block_comment(lexer);
    return !lexer->diag->has_error;
  }
  if (is_ident_start((unsigned char)c)) {
    return lex_identifier(lexer);
  }
  if (isdigit((unsigned char)c)) {
    return lex_number(lexer);
  }
  if (c == '"') {
    return lex_string(lexer);
  }
  if (c == '\'') {
    return lex_char(lexer);
  }

  switch (c) {
  case '(':
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_LPAREN, start, line, column));
  case ')':
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_RPAREN, start, line, column));
  case ',':
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_COMMA, start, line, column));
  case ':':
    advance(lexer);
    if (peek(lexer) == ':') {
      advance(lexer);
      return push_token(
          lexer, make_token(lexer, PIE_TOK_COLON_COLON, start, line, column));
    }
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_COLON, start, line, column));
  case '-':
    advance(lexer);
    if (peek(lexer) == '-') {
      advance(lexer);
      return push_token(
          lexer, make_token(lexer, PIE_TOK_MINUS_MINUS, start, line, column));
    }
    if (peek(lexer) == '>') {
      advance(lexer);
      return push_token(lexer,
                        make_token(lexer, PIE_TOK_ARROW, start, line, column));
    }
    if (peek(lexer) == '=') {
      advance(lexer);
      return push_token(
          lexer, make_token(lexer, PIE_TOK_MINUS_EQ, start, line, column));
    }
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_MINUS, start, line, column));
  case '+':
    advance(lexer);
    if (peek(lexer) == '=') {
      advance(lexer);
      return push_token(
          lexer, make_token(lexer, PIE_TOK_PLUS_EQ, start, line, column));
    }
    if (peek(lexer) == '+') {
      advance(lexer);
      return push_token(
          lexer, make_token(lexer, PIE_TOK_PLUS_PLUS, start, line, column));
    }
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_PLUS, start, line, column));
  case '*':
    advance(lexer);
    if (peek(lexer) == '*') {
      advance(lexer);
      if (peek(lexer) == '=') {
        advance(lexer);
        return push_token(lexer, make_token(lexer, PIE_TOK_STAR_STAR_EQ, start,
                                            line, column));
      }
      return push_token(
          lexer, make_token(lexer, PIE_TOK_STAR_STAR, start, line, column));
    }
    if (peek(lexer) == '=') {
      advance(lexer);
      return push_token(
          lexer, make_token(lexer, PIE_TOK_STAR_EQ, start, line, column));
    }
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_STAR, start, line, column));
  case '/':
    advance(lexer);
    if (peek(lexer) == '=') {
      advance(lexer);
      return push_token(
          lexer, make_token(lexer, PIE_TOK_SLASH_EQ, start, line, column));
    }
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_SLASH, start, line, column));
  case '%':
    advance(lexer);
    if (peek(lexer) == '=') {
      advance(lexer);
      return push_token(
          lexer, make_token(lexer, PIE_TOK_PERCENT_EQ, start, line, column));
    }
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_PERCENT, start, line, column));
  case '.':
    advance(lexer);
    if (peek(lexer) == '.') {
      advance(lexer);
      if (peek(lexer) == '=') {
        advance(lexer);
        return push_token(
            lexer, make_token(lexer, PIE_TOK_DOT_DOT_EQ, start, line, column));
      }
      return push_token(
          lexer, make_token(lexer, PIE_TOK_DOT_DOT, start, line, column));
    }
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_DOT, start, line, column));
  case '&':
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_AMP, start, line, column));
  case '|':
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_PIPE, start, line, column));
  case '^':
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_CARET, start, line, column));
  case '~':
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_TILDE, start, line, column));
  case '=':
    advance(lexer);
    if (peek(lexer) == '=') {
      advance(lexer);
      return push_token(lexer,
                        make_token(lexer, PIE_TOK_EQ_EQ, start, line, column));
    }
    if (peek(lexer) == '>') {
      advance(lexer);
      return push_token(lexer,
                        make_token(lexer, PIE_TOK_FAT_ARROW, start, line, column));
    }
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_EQ, start, line, column));
  case '!':
    advance(lexer);
    if (peek(lexer) == '=') {
      advance(lexer);
      return push_token(
          lexer, make_token(lexer, PIE_TOK_BANG_EQ, start, line, column));
    }
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_BANG, start, line, column));
  case '?':
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_QUESTION, start, line, column));
  case '<':
    advance(lexer);
    if (peek(lexer) == '=') {
      advance(lexer);
      return push_token(lexer,
                        make_token(lexer, PIE_TOK_LT_EQ, start, line, column));
    }
    if (peek(lexer) == '<') {
      advance(lexer);
      return push_token(lexer,
                        make_token(lexer, PIE_TOK_LT_LT, start, line, column));
    }
    if (peek(lexer) == '-') {
      advance(lexer);
      return push_token(lexer,
                        make_token(lexer, PIE_TOK_LARROW, start, line, column));
    }
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_LT, start, line, column));
  case '>':
    advance(lexer);
    if (peek(lexer) == '=') {
      advance(lexer);
      return push_token(lexer,
                        make_token(lexer, PIE_TOK_GT_EQ, start, line, column));
    }
    if (peek(lexer) == '>') {
      advance(lexer);
      return push_token(lexer,
                        make_token(lexer, PIE_TOK_GT_GT, start, line, column));
    }
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_GT, start, line, column));
  case '[':
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_LBRACKET, start, line, column));
  case ']':
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_RBRACKET, start, line, column));
  case '{':
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_LBRACE, start, line, column));
  case '}':
    advance(lexer);
    return push_token(lexer,
                      make_token(lexer, PIE_TOK_RBRACE, start, line, column));
  }

  pie_diag_errorf(lexer->diag, "%d:%d: unexpected character '%c'", line, column,
                  c);
  return 0;
}

int pie_lex_source(const PieSource *source, PieTokenList *tokens,
                   PieDiagnosticBag *diag) {
  pie_token_list_init(tokens);
  Lexer lexer;
  lexer.source = source;
  lexer.p = source->text;
  lexer.line = 1;
  lexer.column = 1;
  lexer.tokens = tokens;
  lexer.diag = diag;

  while (peek(&lexer)) {
    if (!lex_one(&lexer)) {
      pie_token_list_free(tokens);
      return 0;
    }
  }

  PieToken eof;
  memset(&eof, 0, sizeof(eof));
  eof.kind = PIE_TOK_EOF;
  eof.start = lexer.p;
  eof.line = lexer.line;
  eof.column = lexer.column;
  if (!pie_token_list_push(tokens, eof)) {
    pie_diag_error(diag, "out of memory while appending EOF token");
    pie_token_list_free(tokens);
    return 0;
  }
  return !diag->has_error;
}
