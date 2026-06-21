#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

static int push_print_arg(PieStmt *stmt, PiePrintArg arg) {
  PiePrintArg *next = (PiePrintArg *)realloc(
      stmt->args, (stmt->arg_count + 1) * sizeof(PiePrintArg));
  if (!next) {
    return 0;
  }
  stmt->args = next;
  stmt->args[stmt->arg_count++] = arg;
  return 1;
}

static void free_partial_print_stmt(PieStmt *stmt) {
  for (size_t i = 0; i < stmt->arg_count; i++) {
    free(stmt->args[i].text);
    pie_expr_free(stmt->args[i].expr);
  }
  free(stmt->args);
}

PieParseResult pie_feature_print_parse_stmt(PieParseContext *ctx,
                                            PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;
  int println = 0;

  if (api->match(parser, PIE_TOK_PRINTLN)) {
    println = 1;
  } else if (!api->match(parser, PIE_TOK_PRINT)) {
    return PIE_PARSE_NO_MATCH;
  }

  if (!api->expect(parser, PIE_TOK_LPAREN, "expected '(' after print")) {
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_PRINT;
  stmt.println = println;

  if (api->match(parser, PIE_TOK_RPAREN)) {
    if (!pie_program_push_stmt(program, stmt)) {
      pie_diag_error(api->diag(parser),
                     "out of memory while storing print statement");
      return PIE_PARSE_ERROR;
    }
    return PIE_PARSE_OK;
  }

  for (;;) {
    api->skip_newlines(parser);
    PiePrintArg arg;
    memset(&arg, 0, sizeof(arg));

    arg.expr = api->parse_expr(parser);
    if (!arg.expr) {
      free_partial_print_stmt(&stmt);
      return PIE_PARSE_ERROR;
    }

    if (!push_print_arg(&stmt, arg)) {
      pie_expr_free(arg.expr);
      free_partial_print_stmt(&stmt);
      pie_diag_error(api->diag(parser),
                     "out of memory while storing print argument");
      return PIE_PARSE_ERROR;
    }

    api->skip_newlines(parser);
    if (api->match(parser, PIE_TOK_COMMA)) {
      continue;
    }
    if (!api->expect(parser, PIE_TOK_RPAREN,
                     "expected ')' after print arguments")) {
      free_partial_print_stmt(&stmt);
      return PIE_PARSE_ERROR;
    }
    break;
  }

  if (!pie_program_push_stmt(program, stmt)) {
    free_partial_print_stmt(&stmt);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing print statement");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
