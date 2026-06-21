#include "pie/core/parser/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PieParseResult pie_feature_map_parse_literal(PieParseContext *ctx,
                                             PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_LBRACE)) {
    return PIE_PARSE_NO_MATCH;
  }

  if (api->match(parser, PIE_TOK_RBRACE)) {
    *out_expr = pie_expr_map();
    if (!*out_expr) {
      pie_diag_error(api->diag(parser),
                     "out of memory while building empty map");
      return PIE_PARSE_ERROR;
    }
    return PIE_PARSE_OK;
  }

  PieExpr *map = pie_expr_map();
  if (!map) {
    pie_diag_error(api->diag(parser), "out of memory while building map");
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);

  while (!api->check(parser, PIE_TOK_RBRACE) &&
         !api->check(parser, PIE_TOK_EOF)) {
    if (map->map_entry_count > 0) {
      if (!api->match(parser, PIE_TOK_COMMA)) {
        api->error_at(parser, api->peek(parser),
                      "expected ',' or '}' in map literal");
        pie_expr_free(map);
        return PIE_PARSE_ERROR;
      }
    }

    api->skip_separators(parser);

    PieExpr *key = api->parse_expr(parser);
    if (!key) {
      pie_expr_free(map);
      return PIE_PARSE_ERROR;
    }

    api->skip_separators(parser);

    if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after map key")) {
      pie_expr_free(key);
      pie_expr_free(map);
      return PIE_PARSE_ERROR;
    }

    api->skip_separators(parser);

    PieExpr *value = api->parse_expr(parser);
    if (!value) {
      pie_expr_free(key);
      pie_expr_free(map);
      return PIE_PARSE_ERROR;
    }

    if (!pie_expr_map_add(map, key, value)) {
      pie_expr_free(key);
      pie_expr_free(value);
      pie_expr_free(map);
      pie_diag_error(api->diag(parser), "out of memory while adding map entry");
      return PIE_PARSE_ERROR;
    }
  }

  if (!api->expect(parser, PIE_TOK_RBRACE, "expected '}' after map literal")) {
    pie_expr_free(map);
    return PIE_PARSE_ERROR;
  }

  *out_expr = map;
  return PIE_PARSE_OK;
}
