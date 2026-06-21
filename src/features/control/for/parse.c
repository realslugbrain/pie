#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

static void free_branch(PieProgram *program) {
  if (!program) {
    return;
  }
  pie_program_free(program);
  free(program);
}

static PieProgram *new_branch(PieParseContext *ctx) {
  PieProgram *program = (PieProgram *)malloc(sizeof(PieProgram));
  if (!program) {
    pie_diag_error(ctx->api->diag(ctx->parser),
                   "out of memory while parsing for body");
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

static char *str_dup(const char *s) {
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, s, len + 1);
  return copy;
}

PieParseResult pie_feature_for_parse_stmt(PieParseContext *ctx,
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

  if (!api->match(parser, PIE_TOK_FOR)) {
    free(label_name);
    return PIE_PARSE_NO_MATCH;
  }

  api->skip_separators(parser);

  if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
    api->error_at(parser, api->peek(parser),
                  "expected variable name after 'for'");
    return PIE_PARSE_ERROR;
  }
  const PieToken *var_token = api->advance(parser);
  char *var_name = api->copy_token_text(var_token);
  if (!var_name) {
    pie_diag_error(api->diag(parser),
                   "out of memory while parsing for variable");
    return PIE_PARSE_ERROR;
  }

  char *second_var = NULL;
  if (api->check(parser, PIE_TOK_COMMA)) {
    api->advance(parser);
    api->skip_separators(parser);
    if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
      api->error_at(parser, api->peek(parser),
                    "expected variable name after ',' in for");
      free(var_name);
      return PIE_PARSE_ERROR;
    }
    const PieToken *second_token = api->advance(parser);
    second_var = api->copy_token_text(second_token);
    if (!second_var) {
      pie_diag_error(api->diag(parser),
                     "out of memory while parsing for variable");
      free(var_name);
      return PIE_PARSE_ERROR;
    }
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_IN, "expected 'in' after for variable")) {
    free(var_name);
    free(second_var);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);

  PieExpr *start_expr = api->parse_expr(parser);
  if (!start_expr) {
    free(var_name);
    free(second_var);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);

  int inclusive = 0;
  PieExpr *end_expr = NULL;
  int is_range = 0;

  if (start_expr->kind == PIE_EXPR_RANGE) {
    end_expr = start_expr->range_end;
    inclusive = start_expr->range_inclusive;
    PieExpr *real_start = start_expr->range_start;
    start_expr->range_start = NULL;
    start_expr->range_end = NULL;
    pie_expr_free(start_expr);
    start_expr = real_start;
    is_range = 1;
  } else if (api->check(parser, PIE_TOK_DOT_DOT) ||
             api->check(parser, PIE_TOK_DOT_DOT_EQ)) {
    is_range = 1;
    if (api->match(parser, PIE_TOK_DOT_DOT_EQ)) {
      inclusive = 1;
    } else {
      api->advance(parser);
      inclusive = 0;
    }
    end_expr = api->parse_expr(parser);
    if (!end_expr) {
      free(var_name);
      free(second_var);
      pie_expr_free(start_expr);
      return PIE_PARSE_ERROR;
    }
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after for range")) {
    free(var_name);
    free(second_var);
    pie_expr_free(start_expr);
    pie_expr_free(end_expr);
    return PIE_PARSE_ERROR;
  }

  PieProgram *body = new_branch(ctx);
  if (!body) {
    free(var_name);
    free(second_var);
    pie_expr_free(start_expr);
    pie_expr_free(end_expr);
    return PIE_PARSE_ERROR;
  }
  if (parse_body(ctx, body) != PIE_PARSE_OK) {
    free(var_name);
    free(second_var);
    pie_expr_free(start_expr);
    pie_expr_free(end_expr);
    free_branch(body);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_END, "expected 'end' after for block")) {
    free(var_name);
    free(second_var);
    pie_expr_free(start_expr);
    pie_expr_free(end_expr);
    free_branch(body);
    return PIE_PARSE_ERROR;
  }

  if (is_range) {
    if (second_var) {
      pie_diag_error(api->diag(parser),
                     "multi-variable for is not supported with ranges");
      free(var_name);
      free(second_var);
      pie_expr_free(start_expr);
      pie_expr_free(end_expr);
      free_branch(body);
      return PIE_PARSE_ERROR;
    }

    PieStmt let_stmt;
    memset(&let_stmt, 0, sizeof(let_stmt));
    let_stmt.kind = PIE_STMT_LET;
    let_stmt.is_mut = 1;
    let_stmt.name = var_name;
    let_stmt.type_annotation = pie_ast_type_simple(PIE_AST_TYPE_INT);
    let_stmt.expr = start_expr;
    if (!pie_program_push_stmt(program, let_stmt)) {
      pie_diag_error(api->diag(parser), "out of memory while storing for init");
      return PIE_PARSE_ERROR;
    }

    PieExpr *cond_left = pie_expr_var(var_name);
    PieExpr *condition;
    if (inclusive) {
      condition = pie_expr_binary_op("<=", cond_left, end_expr);
    } else {
      condition = pie_expr_binary('<', cond_left, end_expr);
    }

    PieStmt while_stmt;
    memset(&while_stmt, 0, sizeof(while_stmt));
    while_stmt.kind = PIE_STMT_WHILE;
    while_stmt.expr = condition;
    while_stmt.then_branch = body;
    while_stmt.label_name = label_name;
    if (!pie_program_push_stmt(program, while_stmt)) {
      pie_expr_free(condition);
      free_branch(body);
      free(label_name);
      pie_diag_error(api->diag(parser),
                     "out of memory while storing for-as-while");
      return PIE_PARSE_ERROR;
    }

    char *inc_name = str_dup(var_name);
    PieExpr *inc_one = pie_expr_int(1);

    PieStmt inc_stmt;
    memset(&inc_stmt, 0, sizeof(inc_stmt));
    inc_stmt.kind = PIE_STMT_ASSIGN;
    inc_stmt.name = inc_name;
    strncpy(inc_stmt.assign_op, "+=", sizeof(inc_stmt.assign_op) - 1);
    inc_stmt.expr = inc_one;
    if (!pie_program_push_stmt(body, inc_stmt)) {
      free(inc_name);
      pie_diag_error(api->diag(parser),
                     "out of memory while storing for increment");
      return PIE_PARSE_ERROR;
    }

    return PIE_PARSE_OK;
  }

  char *coll_ref = str_dup("_pie_iter_coll");
  PieStmt coll_let;
  memset(&coll_let, 0, sizeof(coll_let));
  coll_let.kind = PIE_STMT_LET;
  coll_let.is_mut = 0;
  coll_let.name = coll_ref;
  coll_let.type_annotation = pie_ast_type_simple(PIE_AST_TYPE_INFER);
  coll_let.expr = start_expr;
  if (!pie_program_push_stmt(program, coll_let)) {
    pie_diag_error(api->diag(parser),
                   "out of memory while storing for collection ref");
    return PIE_PARSE_ERROR;
  }

  char *idx_name = str_dup("_pie_iter_idx");
  PieStmt idx_let;
  memset(&idx_let, 0, sizeof(idx_let));
  idx_let.kind = PIE_STMT_LET;
  idx_let.is_mut = 1;
  idx_let.name = idx_name;
  idx_let.type_annotation = pie_ast_type_simple(PIE_AST_TYPE_INT);
  idx_let.expr = pie_expr_int(0);
  if (!pie_program_push_stmt(program, idx_let)) {
    free(idx_name);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing for index init");
    return PIE_PARSE_ERROR;
  }

  if (second_var) {
    char *len_name = str_dup("_pie_iter_len");
    PieExpr *coll_for_len = pie_expr_var(coll_ref);
    PieExpr *len_call = pie_expr_method_call(coll_for_len, "len");
    PieStmt len_let;
    memset(&len_let, 0, sizeof(len_let));
    len_let.kind = PIE_STMT_LET;
    len_let.is_mut = 0;
    len_let.name = len_name;
    len_let.type_annotation = pie_ast_type_simple(PIE_AST_TYPE_INT);
    len_let.expr = len_call;
    if (!pie_program_push_stmt(program, len_let)) {
      free(len_name);
      free(idx_name);
      free(coll_ref);
      pie_diag_error(api->diag(parser),
                     "out of memory while storing for length");
      return PIE_PARSE_ERROR;
    }

    PieExpr *idx_for_cond = pie_expr_var(idx_name);
    PieExpr *len_for_cond = pie_expr_var(len_name);
    PieExpr *while_cond = pie_expr_binary('<', idx_for_cond, len_for_cond);

    PieProgram *while_body = new_branch(ctx);
    if (!while_body) {
      free(len_name);
      free(idx_name);
      free(coll_ref);
      pie_expr_free(while_cond);
      return PIE_PARSE_ERROR;
    }

    PieExpr *idx_for_key = pie_expr_var(idx_name);

    PieStmt key_let;
    memset(&key_let, 0, sizeof(key_let));
    key_let.kind = PIE_STMT_LET;
    key_let.is_mut = 0;
    key_let.name = var_name;
    key_let.type_annotation = pie_ast_type_simple(PIE_AST_TYPE_INT);
    key_let.expr = idx_for_key;
    if (!pie_program_push_stmt(while_body, key_let)) {
      free(len_name);
      free(idx_name);
      free(coll_ref);
      pie_expr_free(while_cond);
      free_branch(while_body);
      return PIE_PARSE_ERROR;
    }

    PieExpr *coll_for_val = pie_expr_var(coll_ref);
    PieExpr *idx_for_val = pie_expr_var(idx_name);
    PieExpr *val_expr = pie_expr_index(coll_for_val, idx_for_val);

    PieStmt val_let;
    memset(&val_let, 0, sizeof(val_let));
    val_let.kind = PIE_STMT_LET;
    val_let.is_mut = 0;
    val_let.name = second_var;
    val_let.type_annotation = pie_ast_type_simple(PIE_AST_TYPE_INFER);
    val_let.expr = val_expr;
    if (!pie_program_push_stmt(while_body, val_let)) {
      free(len_name);
      free(idx_name);
      free(coll_ref);
      pie_expr_free(while_cond);
      free_branch(while_body);
      return PIE_PARSE_ERROR;
    }

    for (size_t i = 0; i < body->stmt_count; i++) {
      if (!pie_program_push_stmt(while_body, body->stmts[i])) {
        free(len_name);
        free(idx_name);
        free(coll_ref);
        pie_expr_free(while_cond);
        free_branch(while_body);
        return PIE_PARSE_ERROR;
      }
      memset(&body->stmts[i], 0, sizeof(PieStmt));
    }

    char *inc_idx_name = str_dup(idx_name);
    PieStmt inc_idx;
    memset(&inc_idx, 0, sizeof(inc_idx));
    inc_idx.kind = PIE_STMT_ASSIGN;
    inc_idx.name = inc_idx_name;
    strncpy(inc_idx.assign_op, "+=", sizeof(inc_idx.assign_op) - 1);
    inc_idx.expr = pie_expr_int(1);
    if (!pie_program_push_stmt(while_body, inc_idx)) {
      free(inc_idx_name);
      free(len_name);
      free(idx_name);
      free(coll_ref);
      pie_expr_free(while_cond);
      free_branch(while_body);
      return PIE_PARSE_ERROR;
    }

    PieStmt while_stmt;
    memset(&while_stmt, 0, sizeof(while_stmt));
    while_stmt.kind = PIE_STMT_WHILE;
    while_stmt.expr = while_cond;
    while_stmt.then_branch = while_body;
    while_stmt.label_name = label_name;
    if (!pie_program_push_stmt(program, while_stmt)) {
      pie_expr_free(while_cond);
      free_branch(while_body);
      free(label_name);
      pie_diag_error(api->diag(parser),
                     "out of memory while storing for-collection-while");
      return PIE_PARSE_ERROR;
    }

    free_branch(body);

    return PIE_PARSE_OK;
  }

  char *len_name = str_dup("_pie_iter_len");
  PieExpr *coll_for_len = pie_expr_var(coll_ref);
  PieExpr *len_call = pie_expr_method_call(coll_for_len, "len");
  PieStmt len_let;
  memset(&len_let, 0, sizeof(len_let));
  len_let.kind = PIE_STMT_LET;
  len_let.is_mut = 0;
  len_let.name = len_name;
  len_let.type_annotation = pie_ast_type_simple(PIE_AST_TYPE_INT);
  len_let.expr = len_call;
  if (!pie_program_push_stmt(program, len_let)) {
    free(len_name);
    pie_diag_error(api->diag(parser), "out of memory while storing for length");
    return PIE_PARSE_ERROR;
  }

  PieExpr *idx_for_cond = pie_expr_var(idx_name);
  PieExpr *len_for_cond = pie_expr_var(len_name);
  PieExpr *while_cond = pie_expr_binary('<', idx_for_cond, len_for_cond);

  PieProgram *while_body = new_branch(ctx);
  if (!while_body) {
    free(len_name);
    free(idx_name);
    free(coll_ref);
    pie_expr_free(while_cond);
    return PIE_PARSE_ERROR;
  }

  PieExpr *coll_for_item = pie_expr_var(coll_ref);
  PieExpr *idx_for_item = pie_expr_var(idx_name);
  PieExpr *item_val = pie_expr_index(coll_for_item, idx_for_item);

  PieStmt item_let;
  memset(&item_let, 0, sizeof(item_let));
  item_let.kind = PIE_STMT_LET;
  item_let.is_mut = 0;
  item_let.name = var_name;
  item_let.type_annotation = pie_ast_type_simple(PIE_AST_TYPE_INFER);
  item_let.expr = item_val;

  if (!pie_program_push_stmt(while_body, item_let)) {
    free(len_name);
    free(idx_name);
    free(coll_ref);
    pie_expr_free(while_cond);
    free_branch(while_body);
    return PIE_PARSE_ERROR;
  }

  for (size_t i = 0; i < body->stmt_count; i++) {
    if (!pie_program_push_stmt(while_body, body->stmts[i])) {
      free(len_name);
      free(idx_name);
      free(coll_ref);
      pie_expr_free(while_cond);
      free_branch(while_body);
      return PIE_PARSE_ERROR;
    }
    memset(&body->stmts[i], 0, sizeof(PieStmt));
  }

  char *inc_idx_name = str_dup(idx_name);
  PieStmt inc_idx;
  memset(&inc_idx, 0, sizeof(inc_idx));
  inc_idx.kind = PIE_STMT_ASSIGN;
  inc_idx.name = inc_idx_name;
  strncpy(inc_idx.assign_op, "+=", sizeof(inc_idx.assign_op) - 1);
  inc_idx.expr = pie_expr_int(1);
  if (!pie_program_push_stmt(while_body, inc_idx)) {
    free(inc_idx_name);
    free(len_name);
    free(idx_name);
    free(coll_ref);
    pie_expr_free(while_cond);
    free_branch(while_body);
    return PIE_PARSE_ERROR;
  }

  PieStmt while_stmt;
  memset(&while_stmt, 0, sizeof(while_stmt));
  while_stmt.kind = PIE_STMT_WHILE;
  while_stmt.expr = while_cond;
  while_stmt.then_branch = while_body;
  while_stmt.label_name = label_name;
  if (!pie_program_push_stmt(program, while_stmt)) {
    pie_expr_free(while_cond);
    free_branch(while_body);
    free(label_name);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing for-collection-while");
    return PIE_PARSE_ERROR;
  }

  free_branch(body);

  return PIE_PARSE_OK;
}
