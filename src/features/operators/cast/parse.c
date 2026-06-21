#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

static PieAstTypeKind parse_type_annotation(const PieParserApi *api,
                                            PieParser *parser, int *out_width) {
  *out_width = PIE_WIDTH_INFER;
  if (api->match(parser, PIE_TOK_INT_TYPE)) {
    return PIE_AST_TYPE_INT;
  }
  if (api->match(parser, PIE_TOK_FLOAT_TYPE)) {
    return PIE_AST_TYPE_FLOAT;
  }
  if (api->match(parser, PIE_TOK_STRING_TYPE)) {
    return PIE_AST_TYPE_STRING;
  }
  if (api->match(parser, PIE_TOK_BOOL_TYPE)) {
    return PIE_AST_TYPE_BOOL;
  }
  if (api->match(parser, PIE_TOK_CHAR_TYPE)) {
    return PIE_AST_TYPE_CHAR;
  }
  if (api->match(parser, PIE_TOK_BYTE_TYPE)) {
    return PIE_AST_TYPE_BYTE;
  }
  if (api->match(parser, PIE_TOK_VOID_TYPE)) {
    return PIE_AST_TYPE_VOID;
  }
  return PIE_AST_TYPE_INFER;
}

PieParseResult pie_feature_cast_parse_expr(PieParseContext *ctx, PieExpr **left,
                                           int min_precedence) {
  (void)min_precedence;
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_AS)) {
    return PIE_PARSE_NO_MATCH;
  }

  int target_width = PIE_WIDTH_INFER;
  PieAstTypeKind target_kind =
      parse_type_annotation(api, parser, &target_width);
  if (target_kind == PIE_AST_TYPE_INFER) {
    api->error_at(parser, api->peek(parser), "expected type after 'as'");
    return PIE_PARSE_ERROR;
  }

  PieExpr *inner = *left;
  *left = pie_expr_cast(inner, target_kind, target_width);
  if (!*left) {
    pie_expr_free(inner);
    api->error_at(parser, api->peek(parser),
                  "out of memory while building cast expression");
    return PIE_PARSE_ERROR;
  }

  return PIE_PARSE_OK;
}
