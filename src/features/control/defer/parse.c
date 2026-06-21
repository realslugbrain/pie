#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

PieParseResult pie_feature_defer_parse_stmt(PieParseContext *ctx,
                                            PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_DEFER)) {
    return PIE_PARSE_NO_MATCH;
  }

  api->skip_separators(parser);

  PieExpr *expr = api->parse_expr(parser);
  if (!expr) {
    api->error_at(parser, api->peek(parser),
                  "expected expression after 'defer'");
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_DEFER;
  stmt.expr = expr;
  if (!pie_program_push_stmt(program, stmt)) {
    pie_expr_free(expr);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing defer statement");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
