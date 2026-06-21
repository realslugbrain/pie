#define _POSIX_C_SOURCE 200809L
#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

PieParseResult pie_feature_break_parse_stmt(PieParseContext *ctx,
                                            PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_BREAK)) {
    return PIE_PARSE_NO_MATCH;
  }

  char *label_name = NULL;
  PieTokenKind next = api->peek(parser)->kind;
  if (next == PIE_TOK_COLON) {
    api->advance(parser);
    if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
      api->error_at(parser, api->peek(parser), "expected label name after ':'");
      free(label_name);
      return PIE_PARSE_ERROR;
    }
    const PieToken *label_tok = api->advance(parser);
    label_name = api->copy_token_text(label_tok);
    next = api->peek(parser)->kind;
  }

  if (next != PIE_TOK_NEWLINE && next != PIE_TOK_COMMA && next != PIE_TOK_END &&
      next != PIE_TOK_ELSE && next != PIE_TOK_EOF) {
    api->error_at(parser, api->peek(parser), "unexpected token after break");
    free(label_name);
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_BREAK;
  stmt.label_name = label_name;
  if (!pie_program_push_stmt(program, stmt)) {
    free(label_name);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing break statement");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
