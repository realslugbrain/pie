#include "pie/core/ast/ast.h"
#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

static void free_interp_parts(char **texts, size_t *text_lens, PieExpr **exprs,
                              size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(texts[i]);
    pie_expr_free(exprs[i]);
  }
  free(texts);
  free(text_lens);
  free(exprs);
}

static char *dup_string(const char *src, size_t len) {
  char *dst = (char *)malloc(len + 1);
  if (!dst)
    return NULL;
  if (len > 0)
    memcpy(dst, src, len);
  dst[len] = '\0';
  return dst;
}

PieParseResult pie_feature_string_interp_parse_expr(PieParseContext *ctx,
                                                    PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  int starts_with_open = api->check(parser, PIE_TOK_STRING_OPEN);
  int starts_with_literal = api->check(parser, PIE_TOK_STRING_LITERAL);

  if (!starts_with_open && !starts_with_literal) {
    return PIE_PARSE_NO_MATCH;
  }

  size_t capacity = 8;
  char **texts = (char **)malloc(capacity * sizeof(char *));
  size_t *text_lens = (size_t *)malloc(capacity * sizeof(size_t));
  PieExpr **exprs = (PieExpr **)malloc(capacity * sizeof(PieExpr *));
  size_t part_count = 0;

  if (!texts || !text_lens || !exprs) {
    free(texts);
    free(text_lens);
    free(exprs);
    return PIE_PARSE_NO_MATCH;
  }

  if (starts_with_literal) {
    const PieToken *tok = api->advance(parser);
    texts[part_count] = dup_string(tok->string_value, tok->string_len);
    text_lens[part_count] = tok->string_len;
    exprs[part_count] = NULL;
    part_count++;

    if (!api->check(parser, PIE_TOK_STRING_OPEN)) {
      *out_expr = pie_expr_string(texts[0], text_lens[0]);
      free(texts[0]);
      free(texts);
      free(text_lens);
      free(exprs);
      if (!*out_expr) {
        pie_diag_error(api->diag(parser), "out of memory");
        return PIE_PARSE_ERROR;
      }
      return PIE_PARSE_OK;
    }
  }

  while (api->check(parser, PIE_TOK_STRING_OPEN)) {
    const PieToken *open_tok = api->advance(parser);

    if (part_count >= capacity) {
      capacity *= 2;
      char **nt = (char **)realloc(texts, capacity * sizeof(char *));
      size_t *nl = (size_t *)realloc(text_lens, capacity * sizeof(size_t));
      PieExpr **ne = (PieExpr **)realloc(exprs, capacity * sizeof(PieExpr *));
      if (!nt || !nl || !ne) {
        free_interp_parts(texts, text_lens, exprs, part_count);
        return PIE_PARSE_ERROR;
      }
      texts = nt;
      text_lens = nl;
      exprs = ne;
    }

    const PieToken *close_tok = api->peek(parser);
    if (close_tok->kind != PIE_TOK_STRING_CLOSE) {
      free_interp_parts(texts, text_lens, exprs, part_count);
      return PIE_PARSE_ERROR;
    }

    const char *open_end = open_tok->start + open_tok->len;
    const char *close_start = close_tok->start;
    size_t expr_len = (size_t)(close_start - open_end);

    texts[part_count] = NULL;
    text_lens[part_count] = 0;

    if (expr_len > 0) {
      exprs[part_count] = api->parse_expr_from_text(parser, open_end, expr_len);
    } else {
      exprs[part_count] = NULL;
    }

    api->advance(parser);
    part_count++;

    if (api->check(parser, PIE_TOK_STRING_LITERAL)) {
      const PieToken *tok = api->advance(parser);
      if (part_count >= capacity) {
        capacity *= 2;
        char **nt = (char **)realloc(texts, capacity * sizeof(char *));
        size_t *nl = (size_t *)realloc(text_lens, capacity * sizeof(size_t));
        PieExpr **ne = (PieExpr **)realloc(exprs, capacity * sizeof(PieExpr *));
        if (!nt || !nl || !ne) {
          free_interp_parts(texts, text_lens, exprs, part_count);
          return PIE_PARSE_ERROR;
        }
        texts = nt;
        text_lens = nl;
        exprs = ne;
      }
      texts[part_count] = dup_string(tok->string_value, tok->string_len);
      text_lens[part_count] = tok->string_len;
      exprs[part_count] = NULL;
      part_count++;
    }
  }

  if (part_count == 1 && exprs[0] == NULL) {
    *out_expr = pie_expr_string(texts[0], text_lens[0]);
    free(texts[0]);
    free(texts);
    free(text_lens);
    free(exprs);
    if (!*out_expr) {
      pie_diag_error(api->diag(parser), "out of memory");
      return PIE_PARSE_ERROR;
    }
    return PIE_PARSE_OK;
  }

  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    free_interp_parts(texts, text_lens, exprs, part_count);
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }
  expr->kind = PIE_EXPR_STRING_INTERP;
  expr->interp_texts = texts;
  expr->interp_text_lens = text_lens;
  expr->interp_exprs = exprs;
  expr->interp_part_count = part_count;

  *out_expr = expr;
  return PIE_PARSE_OK;
}
