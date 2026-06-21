#include "pie/core/ast/ast.h"
#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

static PieThreadOp parse_thread_op(const char *name) {
  if (strcmp(name, "spawn") == 0)
    return PIE_THREAD_SPAWN;
  if (strcmp(name, "join") == 0)
    return PIE_THREAD_JOIN;
  if (strcmp(name, "mutex") == 0)
    return PIE_THREAD_MUTEX_CREATE;
  if (strcmp(name, "mutex_lock") == 0)
    return PIE_THREAD_MUTEX_LOCK;
  if (strcmp(name, "mutex_unlock") == 0)
    return PIE_THREAD_MUTEX_UNLOCK;
  if (strcmp(name, "sleep") == 0)
    return PIE_THREAD_SLEEP;
  if (strcmp(name, "channel") == 0)
    return PIE_THREAD_CHANNEL_CREATE;
  if (strcmp(name, "channel_send") == 0)
    return PIE_THREAD_CHANNEL_SEND;
  if (strcmp(name, "channel_recv") == 0)
    return PIE_THREAD_CHANNEL_RECV;
  if (strcmp(name, "channel_close") == 0)
    return PIE_THREAD_CHANNEL_CLOSE;
  return -1;
}

PieParseResult pie_feature_threads_parse_expr(PieParseContext *ctx,
                                              PieExpr **out_expr,
                                              int min_precedence) {
  (void)min_precedence;
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
    return PIE_PARSE_NO_MATCH;
  }

  const PieToken *name_token = api->peek(parser);
  size_t name_len = name_token->len;
  if (name_len != 6 || memcmp(name_token->start, "thread", 6) != 0) {
    return PIE_PARSE_NO_MATCH;
  }

  if (api->peek_n(parser, 1)->kind != PIE_TOK_DOT) {
    return PIE_PARSE_NO_MATCH;
  }

  api->advance(parser);
  api->advance(parser);

  if (api->peek(parser)->kind != PIE_TOK_IDENTIFIER) {
    api->error_at(parser, api->peek(parser),
                  "expected thread operation name after 'thread.'");
    return PIE_PARSE_ERROR;
  }

  const PieToken *op_token = api->peek(parser);
  char *op_name_raw = api->copy_token_text(op_token);
  PieThreadOp op = parse_thread_op(op_name_raw);
  free(op_name_raw);
  if (op < 0) {
    api->error_at(parser, api->peek(parser), "unknown thread operation");
    return PIE_PARSE_ERROR;
  }

  api->advance(parser);

  PieExpr *call = pie_expr_thread_call(op);
  if (!call) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  if (!api->expect(parser, PIE_TOK_LPAREN,
                   "expected '(' after thread operation")) {
    pie_expr_free(call);
    return PIE_PARSE_ERROR;
  }

  if (!api->check(parser, PIE_TOK_RPAREN)) {
    do {
      api->skip_newlines(parser);
      PieExpr *arg = api->parse_expr(parser);
      if (!arg) {
        pie_expr_free(call);
        return PIE_PARSE_ERROR;
      }
      if (!pie_expr_thread_call_add_arg(call, arg)) {
        pie_expr_free(arg);
        pie_expr_free(call);
        pie_diag_error(api->diag(parser), "out of memory");
        return PIE_PARSE_ERROR;
      }
      api->skip_newlines(parser);
    } while (api->match(parser, PIE_TOK_COMMA));
  }

  if (!api->expect(parser, PIE_TOK_RPAREN,
                   "expected ')' after thread call arguments")) {
    pie_expr_free(call);
    return PIE_PARSE_ERROR;
  }

  *out_expr = call;
  return PIE_PARSE_OK;
}
