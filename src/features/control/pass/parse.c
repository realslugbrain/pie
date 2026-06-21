#include "pie/core/parser/parser.h"

#include <string.h>

PieParseResult pie_feature_pass_parse_stmt(PieParseContext *ctx,
                                           PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_PASS)) {
    return PIE_PARSE_NO_MATCH;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_PASS;
  if (!pie_program_push_stmt(program, stmt)) {
    pie_diag_error(api->diag(parser),
                   "out of memory while storing pass statement");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
