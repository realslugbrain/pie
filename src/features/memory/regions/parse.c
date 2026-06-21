#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

static void free_body(PieProgram *program) {
  if (!program) {
    return;
  }
  pie_program_free(program);
  free(program);
}

static PieProgram *new_body(PieParseContext *ctx) {
  PieProgram *program = (PieProgram *)malloc(sizeof(PieProgram));
  if (!program) {
    pie_diag_error(ctx->api->diag(ctx->parser),
                   "out of memory while parsing region body");
    return NULL;
  }
  pie_program_init(program);
  return program;
}

static PieParseResult parse_body(PieParseContext *ctx, PieProgram *body) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  for (;;) {
    api->skip_separators(parser);
    if (api->check(parser, PIE_TOK_EOF) || api->check(parser, PIE_TOK_END)) {
      return PIE_PARSE_OK;
    }
    if (!api->parse_statement(parser, body)) {
      return PIE_PARSE_ERROR;
    }
  }
}

PieParseResult pie_feature_regions_parse_stmt(PieParseContext *ctx,
                                              PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_REGION)) {
    return PIE_PARSE_NO_MATCH;
  }

  const PieToken *name_token = api->peek(parser);
  if (!api->expect(parser, PIE_TOK_IDENTIFIER, "expected region name")) {
    return PIE_PARSE_ERROR;
  }
  if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after region name")) {
    return PIE_PARSE_ERROR;
  }

  PieProgram *body = new_body(ctx);
  if (!body) {
    return PIE_PARSE_ERROR;
  }
  if (parse_body(ctx, body) != PIE_PARSE_OK) {
    free_body(body);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_END, "expected 'end' after region block")) {
    free_body(body);
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_REGION;
  stmt.name = api->copy_token_text(name_token);
  stmt.then_branch = body;
  if (!stmt.name) {
    free_body(body);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing region name");
    return PIE_PARSE_ERROR;
  }
  if (!pie_program_push_stmt(program, stmt)) {
    free(stmt.name);
    free_body(body);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing region statement");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
