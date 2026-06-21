#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

static void free_branch(PieProgram *program) {
  if (!program)
    return;
  pie_program_free(program);
  free(program);
}

static PieProgram *new_body(PieParseContext *ctx) {
  PieProgram *program = (PieProgram *)malloc(sizeof(PieProgram));
  if (!program) {
    pie_diag_error(ctx->api->diag(ctx->parser),
                   "out of memory while parsing do body");
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
    if (api->check(parser, PIE_TOK_EOF)) {
      return PIE_PARSE_OK;
    }
    if (api->check(parser, PIE_TOK_WHILE)) {
      return PIE_PARSE_OK;
    }
    if (!api->parse_statement(parser, body)) {
      return PIE_PARSE_ERROR;
    }
  }
}

PieParseResult pie_feature_do_while_parse_stmt(PieParseContext *ctx,
                                               PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  char *label_name = NULL;
  if (api->check(parser, PIE_TOK_COLON)) {
    api->advance(parser);
    if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
      api->error_at(parser, api->peek(parser), "expected label name after ':'");
      return PIE_PARSE_ERROR;
    }
    const PieToken *label_tok = api->advance(parser);
    label_name = api->copy_token_text(label_tok);
    api->skip_separators(parser);
  }

  if (!api->match(parser, PIE_TOK_DO)) {
    free(label_name);
    return PIE_PARSE_NO_MATCH;
  }

  if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after 'do'")) {
    return PIE_PARSE_ERROR;
  }

  PieProgram *body = new_body(ctx);
  if (!body) {
    return PIE_PARSE_ERROR;
  }
  if (parse_body(ctx, body) != PIE_PARSE_OK) {
    free_branch(body);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_WHILE, "expected 'while' after do body")) {
    free_branch(body);
    return PIE_PARSE_ERROR;
  }

  PieExpr *condition = api->parse_expr(parser);
  if (!condition) {
    free_branch(body);
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_DO_WHILE;
  stmt.expr = condition;
  stmt.then_branch = body;
  stmt.label_name = label_name;
  if (!pie_program_push_stmt(program, stmt)) {
    pie_expr_free(condition);
    free_branch(body);
    free(label_name);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing do-while statement");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
