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

static size_t find_condition_colon(PieParseContext *ctx) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;
  size_t start = api->pos(parser);
  int paren = 0;

  for (size_t i = start;; i++) {
    PieTokenKind kind = api->peek_n(parser, i - start)->kind;
    if (kind == PIE_TOK_EOF || kind == PIE_TOK_END || kind == PIE_TOK_NEWLINE) {
      return (size_t)-1;
    }
    if (kind == PIE_TOK_LPAREN) {
      paren++;
    } else if (kind == PIE_TOK_RPAREN && paren > 0) {
      paren--;
    } else if (kind == PIE_TOK_COLON && paren == 0) {
      return i;
    }
  }
}

static PieProgram *new_branch(PieParseContext *ctx) {
  PieProgram *program = (PieProgram *)malloc(sizeof(PieProgram));
  if (!program) {
    pie_diag_error(ctx->api->diag(ctx->parser),
                   "out of memory while parsing if branch");
    return NULL;
  }
  pie_program_init(program);
  return program;
}

static PieParseResult parse_branch_until(PieParseContext *ctx,
                                         PieProgram *branch) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  for (;;) {
    api->skip_separators(parser);
    if (api->check(parser, PIE_TOK_EOF) || api->check(parser, PIE_TOK_END) ||
        api->check(parser, PIE_TOK_ELSE) || api->check(parser, PIE_TOK_ELIF)) {
      return PIE_PARSE_OK;
    }
    if (!api->parse_statement(parser, branch)) {
      return PIE_PARSE_ERROR;
    }
  }
}

static PieParseResult parse_elif_chain(PieParseContext *ctx,
                                       PieProgram *else_branch);

static PieParseResult parse_elif_chain(PieParseContext *ctx,
                                       PieProgram *else_branch) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  for (;;) {
    api->skip_separators(parser);
    if (!api->check(parser, PIE_TOK_ELIF)) {
      break;
    }

    api->advance(parser);

    size_t colon = find_condition_colon(ctx);
    if (colon == (size_t)-1) {
      api->error_at(parser, api->peek(parser),
                    "expected ':' after elif condition");
      return PIE_PARSE_ERROR;
    }

    PieExpr *condition = api->parse_expr_until(parser, colon);
    if (!condition) {
      return PIE_PARSE_ERROR;
    }
    api->set_pos(parser, colon);
    if (!api->expect(parser, PIE_TOK_COLON,
                     "expected ':' after elif condition")) {
      pie_expr_free(condition);
      return PIE_PARSE_ERROR;
    }

    PieProgram *elif_then = new_branch(ctx);
    if (!elif_then) {
      pie_expr_free(condition);
      return PIE_PARSE_ERROR;
    }
    if (parse_branch_until(ctx, elif_then) != PIE_PARSE_OK) {
      pie_expr_free(condition);
      free_branch(elif_then);
      return PIE_PARSE_ERROR;
    }

    PieProgram *elif_else = new_branch(ctx);
    if (!elif_else) {
      pie_expr_free(condition);
      free_branch(elif_then);
      return PIE_PARSE_ERROR;
    }

    if (parse_elif_chain(ctx, elif_else) != PIE_PARSE_OK) {
      pie_expr_free(condition);
      free_branch(elif_then);
      free_branch(elif_else);
      return PIE_PARSE_ERROR;
    }

    api->skip_separators(parser);
    if (api->check(parser, PIE_TOK_ELSE)) {
      api->advance(parser);
      if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after else")) {
        pie_expr_free(condition);
        free_branch(elif_then);
        free_branch(elif_else);
        return PIE_PARSE_ERROR;
      }
      if (parse_branch_until(ctx, elif_else) != PIE_PARSE_OK) {
        pie_expr_free(condition);
        free_branch(elif_then);
        free_branch(elif_else);
        return PIE_PARSE_ERROR;
      }
    }

    PieStmt elif_stmt;
    memset(&elif_stmt, 0, sizeof(elif_stmt));
    elif_stmt.kind = PIE_STMT_IF;
    elif_stmt.expr = condition;
    elif_stmt.then_branch = elif_then;
    elif_stmt.else_branch = elif_else;
    if (!pie_program_push_stmt(else_branch, elif_stmt)) {
      pie_expr_free(condition);
      free_branch(elif_then);
      free_branch(elif_else);
      pie_diag_error(api->diag(parser),
                     "out of memory while storing elif statement");
      return PIE_PARSE_ERROR;
    }
  }

  return PIE_PARSE_OK;
}

PieParseResult pie_feature_if_parse_stmt(PieParseContext *ctx,
                                         PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_IF)) {
    return PIE_PARSE_NO_MATCH;
  }

  if (api->check(parser, PIE_TOK_LET)) {
    api->advance(parser);

    char *enum_name = NULL;
    char *variant_name = NULL;
    char **bindings = NULL;
    size_t binding_count = 0;

    if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
      api->error_at(parser, api->peek(parser),
                    "expected variant pattern after 'let'");
      return PIE_PARSE_ERROR;
    }

    const PieToken *variant_tok = api->peek(parser);

    int is_bare =
        (variant_tok->len == 4 && memcmp(variant_tok->start, "Some", 4) == 0) ||
        (variant_tok->len == 4 && memcmp(variant_tok->start, "None", 4) == 0) ||
        (variant_tok->len == 2 && memcmp(variant_tok->start, "Ok", 2) == 0) ||
        (variant_tok->len == 3 && memcmp(variant_tok->start, "Err", 3) == 0);

    if (is_bare) {
      variant_name = api->copy_token_text(variant_tok);
      int is_option =
          (variant_name[0] == 'S' || strcmp(variant_name, "None") == 0);
      const char *raw = is_option ? "Option" : "Result";
      size_t raw_len = strlen(raw);
      enum_name = (char *)malloc(raw_len + 1);
      memcpy(enum_name, raw, raw_len + 1);
      api->advance(parser);
    } else if (api->peek_n(parser, 1)->kind == PIE_TOK_DOT) {

      enum_name = api->copy_token_text(variant_tok);
      api->advance(parser);
      api->advance(parser);
      variant_tok = api->peek(parser);
      variant_name = api->copy_token_text(variant_tok);
      api->advance(parser);
    } else {
      api->error_at(parser, api->peek(parser),
                    "expected variant pattern in if-let");
      return PIE_PARSE_ERROR;
    }

    if (api->match(parser, PIE_TOK_LPAREN)) {
      size_t bind_cap = 4;
      bindings = (char **)calloc(bind_cap, sizeof(char *));
      while (!api->check(parser, PIE_TOK_RPAREN)) {
        if (binding_count > 0) {
          if (!api->match(parser, PIE_TOK_COMMA)) {
            api->error_at(parser, api->peek(parser), "expected ',' or ')'");
            for (size_t j = 0; j < binding_count; j++)
              free(bindings[j]);
            free(bindings);
            free(enum_name);
            free(variant_name);
            return PIE_PARSE_ERROR;
          }
        }
        if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
          api->error_at(parser, api->peek(parser), "expected binding name");
          for (size_t j = 0; j < binding_count; j++)
            free(bindings[j]);
          free(bindings);
          free(enum_name);
          free(variant_name);
          return PIE_PARSE_ERROR;
        }
        if (binding_count >= bind_cap) {
          bind_cap *= 2;
          char **new_b = (char **)realloc(bindings, bind_cap * sizeof(char *));
          bindings = new_b;
        }
        const PieToken *bind_tok = api->advance(parser);
        bindings[binding_count] = api->copy_token_text(bind_tok);
        binding_count++;
      }
      if (!api->expect(parser, PIE_TOK_RPAREN, "expected ')' after bindings")) {
        for (size_t j = 0; j < binding_count; j++)
          free(bindings[j]);
        free(bindings);
        free(enum_name);
        free(variant_name);
        return PIE_PARSE_ERROR;
      }
    }

    if (!api->expect(parser, PIE_TOK_EQ, "expected '=' in if-let")) {
      for (size_t j = 0; j < binding_count; j++)
        free(bindings[j]);
      free(bindings);
      free(enum_name);
      free(variant_name);
      return PIE_PARSE_ERROR;
    }

    size_t colon = find_condition_colon(ctx);
    if (colon == (size_t)-1) {
      api->error_at(parser, api->peek(parser),
                    "expected ':' after if-let expression");
      for (size_t j = 0; j < binding_count; j++)
        free(bindings[j]);
      free(bindings);
      free(enum_name);
      free(variant_name);
      return PIE_PARSE_ERROR;
    }

    PieExpr *target = api->parse_expr_until(parser, colon);
    if (!target) {
      for (size_t j = 0; j < binding_count; j++)
        free(bindings[j]);
      free(bindings);
      free(enum_name);
      free(variant_name);
      return PIE_PARSE_ERROR;
    }
    api->set_pos(parser, colon);
    if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after if-let")) {
      pie_expr_free(target);
      for (size_t j = 0; j < binding_count; j++)
        free(bindings[j]);
      free(bindings);
      free(enum_name);
      free(variant_name);
      return PIE_PARSE_ERROR;
    }

    PieProgram *then_branch = new_branch(ctx);
    if (!then_branch) {
      pie_expr_free(target);
      for (size_t j = 0; j < binding_count; j++)
        free(bindings[j]);
      free(bindings);
      free(enum_name);
      free(variant_name);
      return PIE_PARSE_ERROR;
    }
    if (parse_branch_until(ctx, then_branch) != PIE_PARSE_OK) {
      pie_expr_free(target);
      free_branch(then_branch);
      for (size_t j = 0; j < binding_count; j++)
        free(bindings[j]);
      free(bindings);
      free(enum_name);
      free(variant_name);
      return PIE_PARSE_ERROR;
    }

    PieProgram *else_branch = new_branch(ctx);
    if (!else_branch) {
      pie_expr_free(target);
      free_branch(then_branch);
      for (size_t j = 0; j < binding_count; j++)
        free(bindings[j]);
      free(bindings);
      free(enum_name);
      free(variant_name);
      return PIE_PARSE_ERROR;
    }
    api->skip_separators(parser);
    if (api->match(parser, PIE_TOK_ELSE)) {
      if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after else")) {
        pie_expr_free(target);
        free_branch(then_branch);
        free_branch(else_branch);
        for (size_t j = 0; j < binding_count; j++)
          free(bindings[j]);
        free(bindings);
        free(enum_name);
        free(variant_name);
        return PIE_PARSE_ERROR;
      }
      if (parse_branch_until(ctx, else_branch) != PIE_PARSE_OK) {
        pie_expr_free(target);
        free_branch(then_branch);
        free_branch(else_branch);
        for (size_t j = 0; j < binding_count; j++)
          free(bindings[j]);
        free(bindings);
        free(enum_name);
        free(variant_name);
        return PIE_PARSE_ERROR;
      }
    }

    api->skip_separators(parser);
    if (!api->expect(parser, PIE_TOK_END,
                     "expected 'end' after if-let block")) {
      pie_expr_free(target);
      free_branch(then_branch);
      free_branch(else_branch);
      for (size_t j = 0; j < binding_count; j++)
        free(bindings[j]);
      free(bindings);
      free(enum_name);
      free(variant_name);
      return PIE_PARSE_ERROR;
    }

    size_t case_name_len = strlen(enum_name) + 1 + strlen(variant_name);
    char *case_name = (char *)malloc(case_name_len + 1);
    memcpy(case_name, enum_name, strlen(enum_name));
    case_name[strlen(enum_name)] = '.';
    memcpy(case_name + strlen(enum_name) + 1, variant_name,
           strlen(variant_name) + 1);

    PieStmt match_stmt;
    memset(&match_stmt, 0, sizeof(match_stmt));
    match_stmt.kind = PIE_STMT_MATCH;
    match_stmt.match_target = target;
    match_stmt.match_case_count = 1;
    match_stmt.match_case_names = (char **)malloc(sizeof(char *));
    match_stmt.match_case_names[0] = case_name;
    match_stmt.match_case_bodies = (PieProgram **)malloc(sizeof(PieProgram *));
    match_stmt.match_case_bodies[0] = then_branch;
    match_stmt.match_case_bindings = (char ***)malloc(sizeof(char **));
    match_stmt.match_case_bindings[0] = bindings;
    match_stmt.match_case_binding_counts = (size_t *)malloc(sizeof(size_t));
    match_stmt.match_case_binding_counts[0] = binding_count;
    if (else_branch->stmt_count > 0) {
      match_stmt.match_default = else_branch;
    } else {
      free_branch(else_branch);
      match_stmt.match_default = NULL;
    }

    if (!pie_program_push_stmt(program, match_stmt)) {
      pie_expr_free(target);
      free(case_name);
      free(enum_name);
      free(variant_name);
      free(match_stmt.match_case_names);
      free(match_stmt.match_case_bodies);
      free(match_stmt.match_case_bindings);
      free(match_stmt.match_case_binding_counts);
      if (match_stmt.match_default) {
        free_branch(match_stmt.match_default);
      }
      pie_diag_error(api->diag(parser),
                     "out of memory while storing if-let statement");
      return PIE_PARSE_ERROR;
    }

    free(enum_name);
    free(variant_name);
    return PIE_PARSE_OK;
  }

  size_t colon = find_condition_colon(ctx);
  if (colon == (size_t)-1) {
    api->error_at(parser, api->peek(parser), "expected ':' after if condition");
    return PIE_PARSE_ERROR;
  }

  PieExpr *condition = api->parse_expr_until(parser, colon);
  if (!condition) {
    return PIE_PARSE_ERROR;
  }
  api->set_pos(parser, colon);
  if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after if condition")) {
    pie_expr_free(condition);
    return PIE_PARSE_ERROR;
  }

  PieProgram *then_branch = new_branch(ctx);
  if (!then_branch) {
    pie_expr_free(condition);
    return PIE_PARSE_ERROR;
  }
  if (parse_branch_until(ctx, then_branch) != PIE_PARSE_OK) {
    pie_expr_free(condition);
    free_branch(then_branch);
    return PIE_PARSE_ERROR;
  }

  PieProgram *else_branch = new_branch(ctx);
  if (!else_branch) {
    pie_expr_free(condition);
    free_branch(then_branch);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);

  if (api->check(parser, PIE_TOK_ELIF)) {
    if (parse_elif_chain(ctx, else_branch) != PIE_PARSE_OK) {
      pie_expr_free(condition);
      free_branch(then_branch);
      free_branch(else_branch);
      return PIE_PARSE_ERROR;
    }
  } else if (api->match(parser, PIE_TOK_ELSE)) {
    if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after else")) {
      pie_expr_free(condition);
      free_branch(then_branch);
      free_branch(else_branch);
      return PIE_PARSE_ERROR;
    }
    if (parse_branch_until(ctx, else_branch) != PIE_PARSE_OK) {
      pie_expr_free(condition);
      free_branch(then_branch);
      free_branch(else_branch);
      return PIE_PARSE_ERROR;
    }
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_END, "expected 'end' after if block")) {
    pie_expr_free(condition);
    free_branch(then_branch);
    free_branch(else_branch);
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_IF;
  stmt.expr = condition;
  stmt.then_branch = then_branch;
  if (else_branch->stmt_count > 0) {
    stmt.else_branch = else_branch;
  } else {
    free_branch(else_branch);
    stmt.else_branch = NULL;
  }
  if (!pie_program_push_stmt(program, stmt)) {
    pie_expr_free(condition);
    free_branch(then_branch);
    if (stmt.else_branch) {
      free_branch(stmt.else_branch);
    }
    pie_diag_error(api->diag(parser),
                   "out of memory while storing if statement");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
