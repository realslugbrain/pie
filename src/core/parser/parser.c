#define _POSIX_C_SOURCE 200809L
#include "pie/core/parser/parser.h"

#include "pie/core/lexer/lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PieParser {
  const PieTokenList *tokens;
  size_t pos;
  PieDiagnosticBag *diag;
};

static const PieParserApi *parser_api(void);
static int api_parse_statement(PieParser *parser, PieProgram *program);
static PieExpr *api_parse_expr_prec(PieParser *parser, int min_precedence);

static const PieToken *api_peek(PieParser *parser) {
  return &parser->tokens->items[parser->pos];
}

static const PieToken *api_peek_n(PieParser *parser, size_t n) {
  size_t index = parser->pos + n;
  if (index >= parser->tokens->count) {
    return &parser->tokens->items[parser->tokens->count - 1];
  }
  return &parser->tokens->items[index];
}

static const PieToken *api_advance(PieParser *parser) {
  const PieToken *token = api_peek(parser);
  if (token->kind != PIE_TOK_EOF) {
    parser->pos++;
  }
  return token;
}

static int api_check(PieParser *parser, PieTokenKind kind) {
  return api_peek(parser)->kind == kind;
}

static int api_match(PieParser *parser, PieTokenKind kind) {
  if (!api_check(parser, kind)) {
    return 0;
  }
  api_advance(parser);
  return 1;
}

static int token_is_separator(PieTokenKind kind) {
  return kind == PIE_TOK_NEWLINE || kind == PIE_TOK_COMMA;
}

static void api_skip_separators(PieParser *parser) {
  while (token_is_separator(api_peek(parser)->kind)) {
    api_advance(parser);
  }
}

static void api_skip_newlines(PieParser *parser) {
  while (api_peek(parser)->kind == PIE_TOK_NEWLINE) {
    api_advance(parser);
  }
}

static char *api_copy_token_text(const PieToken *token) {
  char *text = (char *)malloc(token->len + 1);
  if (!text) {
    return NULL;
  }
  memcpy(text, token->start, token->len);
  text[token->len] = '\0';
  return text;
}

static void api_error_at(PieParser *parser, const PieToken *token,
                         const char *message) {
  pie_diag_errorf(parser->diag, "%d:%d: %s near %s", token->line, token->column,
                  message, pie_token_kind_name(token->kind));
}

static int api_expect(PieParser *parser, PieTokenKind kind,
                      const char *message) {
  if (api_match(parser, kind)) {
    return 1;
  }
  api_error_at(parser, api_peek(parser), message);
  return 0;
}

static size_t api_find_stmt_end(PieParser *parser, size_t start) {
  int paren = 0;
  int bracket = 0;
  int brace = 0;
  int fn_depth = 0;
  for (size_t i = start; i < parser->tokens->count; i++) {
    PieTokenKind kind = parser->tokens->items[i].kind;
    if (kind == PIE_TOK_LPAREN) {
      paren++;
    } else if (kind == PIE_TOK_RPAREN && paren > 0) {
      paren--;
    } else if (kind == PIE_TOK_LBRACKET) {
      bracket++;
    } else if (kind == PIE_TOK_RBRACKET && bracket > 0) {
      bracket--;
    } else if (kind == PIE_TOK_LBRACE) {
      brace++;
    } else if (kind == PIE_TOK_RBRACE && brace > 0) {
      brace--;
    } else if (kind == PIE_TOK_FN && paren == 0 && bracket == 0 && brace == 0) {
      fn_depth++;
    } else if (kind == PIE_TOK_END && fn_depth > 0) {
      fn_depth--;
    } else if (paren == 0 && bracket == 0 && brace == 0 && fn_depth == 0 &&
               (kind == PIE_TOK_NEWLINE || kind == PIE_TOK_COMMA ||
                kind == PIE_TOK_END || kind == PIE_TOK_EOF)) {
      return i;
    }
  }
  return parser->tokens->count - 1;
}

static void api_skip_to_stmt_end(PieParser *parser) {
  parser->pos = api_find_stmt_end(parser, parser->pos);
  if (token_is_separator(api_peek(parser)->kind)) {
    api_advance(parser);
  }
}

static size_t api_pos(PieParser *parser) { return parser->pos; }

static void api_set_pos(PieParser *parser, size_t pos) { parser->pos = pos; }

static PieDiagnosticBag *api_diag(PieParser *parser) { return parser->diag; }

static PieParseContext make_context(PieParser *parser) {
  PieParseContext ctx;
  ctx.parser = parser;
  ctx.api = parser_api();
  return ctx;
}

static PieExpr *parse_expr_prefix(PieParser *parser) {
  const PieParseHookRegistry *registry = pie_parse_hook_registry();
  PieParseContext ctx = make_context(parser);

  for (size_t i = 0; i < registry->expr_prefix_hook_count; i++) {
    size_t start = parser->pos;
    PieExpr *expr = NULL;
    PieParseResult result = registry->expr_prefix_hooks[i].parse(&ctx, &expr);
    if (result != PIE_PARSE_NO_MATCH) {
    }
    if (result == PIE_PARSE_OK) {
      return expr;
    }
    if (result == PIE_PARSE_ERROR) {
      pie_expr_free(expr);
      return NULL;
    }
    parser->pos = start;
  }

  if (api_check(parser, PIE_TOK_LPAREN)) {
    api_advance(parser);
    PieExpr *first = api_parse_expr_prec(parser, 0);
    if (!first) {
      return NULL;
    }
    if (api_check(parser, PIE_TOK_COMMA)) {
      PieExpr *tuple = pie_expr_tuple(0);
      if (!tuple) {
        pie_expr_free(first);
        return NULL;
      }
      pie_expr_tuple_add_element(tuple, first);
      while (api_match(parser, PIE_TOK_COMMA)) {
        if (api_check(parser, PIE_TOK_RPAREN))
          break;
        PieExpr *elem = api_parse_expr_prec(parser, 0);
        if (!elem) {
          pie_expr_free(tuple);
          return NULL;
        }
        pie_expr_tuple_add_element(tuple, elem);
      }
      if (!api_expect(parser, PIE_TOK_RPAREN,
                      "expected ')' after tuple elements")) {
        pie_expr_free(tuple);
        return NULL;
      }
      return tuple;
    }
    if (!api_expect(parser, PIE_TOK_RPAREN, "expected ')' after expression")) {
      pie_expr_free(first);
      return NULL;
    }
    return first;
  }

  if (api_check(parser, PIE_TOK_STRING_LITERAL)) {
    api_error_at(parser, api_peek(parser),
                 "string expressions are not implemented yet");
  } else {
    api_error_at(parser, api_peek(parser), "expected expression");
  }
  return NULL;
}

static PieExpr *api_parse_expr_prec(PieParser *parser, int min_precedence) {
  PieExpr *left = parse_expr_prefix(parser);
  if (!left) {
    return NULL;
  }

  for (;;) {
    const PieParseHookRegistry *registry = pie_parse_hook_registry();
    PieParseContext ctx = make_context(parser);
    int matched = 0;

    for (size_t i = 0; i < registry->expr_infix_hook_count; i++) {
      size_t start = parser->pos;
      PieExpr *candidate = left;
      PieParseResult result =
          registry->expr_infix_hooks[i].parse(&ctx, &candidate, min_precedence);
      if (result == PIE_PARSE_OK) {
        left = candidate;
        matched = 1;
        break;
      }
      if (result == PIE_PARSE_ERROR) {
        if (candidate != left) {
          pie_expr_free(candidate);
        } else {
          pie_expr_free(left);
        }
        return NULL;
      }
      parser->pos = start;
    }

    if (!matched) {
      return left;
    }
  }
}

static PieExpr *api_parse_expr(PieParser *parser) {
  return api_parse_expr_prec(parser, 0);
}

static PieExpr *api_parse_expr_until(PieParser *parser, size_t end) {
  PieExpr *expr = api_parse_expr_prec(parser, 0);
  if (!expr) {
    return NULL;
  }
  if (parser->pos != end) {
    api_error_at(parser, api_peek(parser),
                 "unexpected trailing expression token");
    pie_expr_free(expr);
    return NULL;
  }
  return expr;
}

static PieExpr *api_parse_expr_from_text(PieParser *parser, const char *text,
                                         size_t len) {
  (void)parser;

  char *buf = (char *)malloc(len + 1);
  if (!buf)
    return NULL;
  memcpy(buf, text, len);
  buf[len] = '\0';

  PieSource source;
  source.text = buf;
  source.len = len;
  source.path = "<interp>";

  PieTokenList tokens;
  PieDiagnosticBag diag;
  pie_diag_init(&diag);

  if (!pie_lex_source(&source, &tokens, &diag)) {
    pie_token_list_free(&tokens);
    free(buf);
    return NULL;
  }

  if (tokens.count == 0 || tokens.items[0].kind == PIE_TOK_EOF) {
    pie_token_list_free(&tokens);
    free(buf);
    return NULL;
  }

  PieParser sub_parser;
  sub_parser.tokens = &tokens;
  sub_parser.pos = 0;
  sub_parser.diag = &diag;

  PieExpr *expr = api_parse_expr_prec(&sub_parser, 0);
  pie_token_list_free(&tokens);
  free(buf);
  return expr;
}

static const PieParserApi PIE_PARSER_API = {
    .peek = api_peek,
    .peek_n = api_peek_n,
    .advance = api_advance,
    .check = api_check,
    .match = api_match,
    .expect = api_expect,
    .skip_separators = api_skip_separators,
    .skip_newlines = api_skip_newlines,
    .skip_to_stmt_end = api_skip_to_stmt_end,
    .find_stmt_end = api_find_stmt_end,
    .pos = api_pos,
    .set_pos = api_set_pos,
    .diag = api_diag,
    .copy_token_text = api_copy_token_text,
    .error_at = api_error_at,
    .parse_expr = api_parse_expr,
    .parse_expr_prec = api_parse_expr_prec,
    .parse_expr_until = api_parse_expr_until,
    .parse_statement = api_parse_statement,
    .parse_expr_from_text = api_parse_expr_from_text,
};

static const PieParserApi *parser_api(void) { return &PIE_PARSER_API; }

static int api_parse_statement(PieParser *parser, PieProgram *program) {
  api_skip_separators(parser);
  if (api_check(parser, PIE_TOK_EOF) || api_check(parser, PIE_TOK_END) ||
      api_check(parser, PIE_TOK_ELSE) || api_check(parser, PIE_TOK_ELIF)) {
    return 1;
  }

  const PieParseHookRegistry *registry = pie_parse_hook_registry();
  PieParseContext ctx = make_context(parser);

  for (size_t i = 0; i < registry->stmt_hook_count; i++) {
    size_t start = parser->pos;
    PieParseResult result = registry->stmt_hooks[i].parse(&ctx, program);
    if (result == PIE_PARSE_OK) {
      return 1;
    }
    if (result == PIE_PARSE_ERROR) {
      return 0;
    }
    parser->pos = start;
  }

  api_error_at(parser, api_peek(parser), "unsupported statement");
  return 0;
}

static int parse_top_level(PieParser *parser, PieProgram *program) {
  const PieParseHookRegistry *registry = pie_parse_hook_registry();
  PieParseContext ctx = make_context(parser);

  while (!api_check(parser, PIE_TOK_EOF)) {
    api_skip_separators(parser);
    if (api_check(parser, PIE_TOK_EOF)) {
      break;
    }

    int matched = 0;
    for (size_t i = 0; i < registry->top_level_hook_count; i++) {
      size_t start = parser->pos;
      PieParseResult result = registry->top_level_hooks[i].parse(&ctx, program);
      if (result == PIE_PARSE_OK) {
        matched = 1;
        break;
      }
      if (result == PIE_PARSE_ERROR) {
        return 0;
      }
      parser->pos = start;
    }

    if (matched) {
      continue;
    }

    if (!api_parse_statement(parser, program)) {
      return 0;
    }
  }
  return 1;
}

int pie_parse_source(const PieSource *source, PieProgram *program,
                     PieDiagnosticBag *diag) {
  pie_program_init(program);

  {
    PieEnumDef option_def;
    memset(&option_def, 0, sizeof(option_def));
    option_def.name = strdup("Option");
    option_def.is_pub = 1;
    option_def.variants[0].name = strdup("None");
    option_def.variants[0].payload_count = 0;
    option_def.variants[1].name = strdup("Some");
    option_def.variants[1].payload_kinds = calloc(1, sizeof(PieAstTypeKind));
    option_def.variants[1].payload_kinds[0] = PIE_AST_TYPE_INFER;
    option_def.variants[1].payload_widths = calloc(1, sizeof(int));
    option_def.variants[1].payload_widths[0] = 0;
    option_def.variants[1].payload_count = 1;
    option_def.variant_count = 2;
    pie_program_push_enum(program, option_def);
  }
  {
    PieEnumDef result_def;
    memset(&result_def, 0, sizeof(result_def));
    result_def.name = strdup("Result");
    result_def.is_pub = 1;
    result_def.variants[0].name = strdup("Ok");
    result_def.variants[0].payload_kinds = calloc(1, sizeof(PieAstTypeKind));
    result_def.variants[0].payload_kinds[0] = PIE_AST_TYPE_INFER;
    result_def.variants[0].payload_widths = calloc(1, sizeof(int));
    result_def.variants[0].payload_widths[0] = 0;
    result_def.variants[0].payload_count = 1;
    result_def.variants[1].name = strdup("Err");
    result_def.variants[1].payload_kinds = calloc(1, sizeof(PieAstTypeKind));
    result_def.variants[1].payload_kinds[0] = PIE_AST_TYPE_INFER;
    result_def.variants[1].payload_widths = calloc(1, sizeof(int));
    result_def.variants[1].payload_widths[0] = 0;
    result_def.variants[1].payload_count = 1;
    result_def.variant_count = 2;
    pie_program_push_enum(program, result_def);
  }

  PieTokenList tokens;
  if (!pie_lex_source(source, &tokens, diag)) {
    return 0;
  }

  PieParser parser;
  parser.tokens = &tokens;
  parser.pos = 0;
  parser.diag = diag;

  int ok = parse_top_level(&parser, program) && !diag->has_error;
  pie_token_list_free(&tokens);
  return ok;
}
