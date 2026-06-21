#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

static char *feature_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s);
  char *copy = malloc(len + 1);
  if (copy) {
    memcpy(copy, s, len + 1);
  }
  return copy;
}

static int parse_struct_fields(PieParseContext *ctx, PieStructDef *def) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  def->fields = NULL;
  def->field_count = 0;
  def->field_capacity = 0;

  for (;;) {
    api->skip_separators(parser);
    if (api->check(parser, PIE_TOK_END) || api->check(parser, PIE_TOK_EOF)) {
      break;
    }

    api->match(parser, PIE_TOK_PUB);

    if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
      api->error_at(parser, api->peek(parser), "expected field name");
      return 0;
    }
    const PieToken *name_token = api->advance(parser);
    char *field_name = api->copy_token_text(name_token);
    if (!field_name) {
      pie_diag_error(api->diag(parser), "out of memory");
      return 0;
    }

    api->skip_separators(parser);
    if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after field name")) {
      free(field_name);
      return 0;
    }

    api->skip_separators(parser);

    PieAstType field_type = pie_ast_type_simple(PIE_AST_TYPE_INFER);
    if (api->check(parser, PIE_TOK_INT_TYPE)) {
      api->advance(parser);
      field_type = pie_ast_type_simple(PIE_AST_TYPE_INT);
    } else if (api->check(parser, PIE_TOK_STRING_TYPE)) {
      api->advance(parser);
      field_type = pie_ast_type_simple(PIE_AST_TYPE_STRING);
    } else if (api->check(parser, PIE_TOK_BOOL_TYPE)) {
      api->advance(parser);
      field_type = pie_ast_type_simple(PIE_AST_TYPE_BOOL);
    } else if (api->check(parser, PIE_TOK_FLOAT_TYPE)) {
      api->advance(parser);
      field_type = pie_ast_type_simple(PIE_AST_TYPE_FLOAT);
    } else if (api->check(parser, PIE_TOK_CHAR_TYPE)) {
      api->advance(parser);
      field_type = pie_ast_type_simple(PIE_AST_TYPE_CHAR);
    } else if (api->check(parser, PIE_TOK_BYTE_TYPE)) {
      api->advance(parser);
      field_type = pie_ast_type_simple(PIE_AST_TYPE_BYTE);
    } else if (api->check(parser, PIE_TOK_IDENTIFIER)) {
      const PieToken *type_token = api->advance(parser);
      char *type_name = api->copy_token_text(type_token);
      if (type_name) {
        field_type.kind = PIE_AST_TYPE_STRUCT;
        field_type.struct_name = type_name;
      }
    } else {
      api->error_at(parser, api->peek(parser), "expected type annotation");
      free(field_name);
      return 0;
    }

    if (def->field_count == def->field_capacity) {
      size_t next = def->field_capacity ? def->field_capacity * 2 : 8;
      PieStructField *next_fields =
          (PieStructField *)realloc(def->fields, next * sizeof(PieStructField));
      if (!next_fields) {
        free(field_name);
        free(field_type.struct_name);
        return 0;
      }
      def->fields = next_fields;
      def->field_capacity = next;
    }
    def->fields[def->field_count].name = field_name;
    def->fields[def->field_count].type = field_type;
    def->field_count++;
  }

  return 1;
}

PieParseResult pie_feature_structs_parse_top_level(PieParseContext *ctx,
                                                   PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  size_t start = api->pos(parser);
  int is_pub = 0;
  if (api->match(parser, PIE_TOK_PUB)) {
    is_pub = 1;
  }

  if (!api->match(parser, PIE_TOK_STRUCT)) {
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }

  api->skip_separators(parser);

  if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
    api->error_at(parser, api->peek(parser), "expected struct name");
    return PIE_PARSE_ERROR;
  }
  const PieToken *name_token = api->advance(parser);
  char *struct_name = api->copy_token_text(name_token);
  if (!struct_name) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after struct name")) {
    free(struct_name);
    return PIE_PARSE_ERROR;
  }

  PieStructDef def;
  memset(&def, 0, sizeof(def));
  def.name = struct_name;
  def.is_pub = is_pub;

  if (!parse_struct_fields(ctx, &def)) {
    free(struct_name);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_END, "expected 'end' after struct fields")) {
    free(def.name);
    for (size_t i = 0; i < def.field_count; i++) {
      free(def.fields[i].name);
      free(def.fields[i].type.struct_name);
    }
    free(def.fields);
    return PIE_PARSE_ERROR;
  }

  if (!pie_program_push_struct(program, def)) {
    free(def.name);
    for (size_t i = 0; i < def.field_count; i++) {
      free(def.fields[i].name);
      free(def.fields[i].type.struct_name);
    }
    free(def.fields);
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_STRUCT;
  stmt.struct_def = (PieStructDef *)malloc(sizeof(PieStructDef));
  if (!stmt.struct_def) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }
  *stmt.struct_def = def;
  stmt.struct_def->name = feature_strdup(def.name);
  stmt.struct_def->fields =
      (PieStructField *)malloc(def.field_count * sizeof(PieStructField));
  for (size_t i = 0; i < def.field_count; i++) {
    stmt.struct_def->fields[i].name = feature_strdup(def.fields[i].name);
    stmt.struct_def->fields[i].type = def.fields[i].type;
    if (def.fields[i].type.struct_name) {
      stmt.struct_def->fields[i].type.struct_name =
          feature_strdup(def.fields[i].type.struct_name);
    }
  }
  if (!pie_program_push_stmt(program, stmt)) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  return PIE_PARSE_OK;
}

PieParseResult pie_feature_structs_parse_new_expr(PieParseContext *ctx,
                                                  PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_NEW)) {
    return PIE_PARSE_NO_MATCH;
  }

  api->skip_separators(parser);

  if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
    api->error_at(parser, api->peek(parser),
                  "expected struct name after 'new'");
    return PIE_PARSE_ERROR;
  }
  const PieToken *name_token = api->advance(parser);
  char *type_name = api->copy_token_text(name_token);
  if (!type_name) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_LPAREN, "expected '(' after struct name")) {
    free(type_name);
    return PIE_PARSE_ERROR;
  }

  PieExpr *expr = pie_expr_new(type_name);
  free(type_name);
  if (!expr) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  api->skip_newlines(parser);
  if (!api->check(parser, PIE_TOK_RPAREN)) {
    for (;;) {
      api->skip_newlines(parser);
      if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
        api->error_at(parser, api->peek(parser), "expected field name");
        pie_expr_free(expr);
        return PIE_PARSE_ERROR;
      }
      const PieToken *field_token = api->advance(parser);
      char *field_name = api->copy_token_text(field_token);
      if (!field_name) {
        pie_expr_free(expr);
        return PIE_PARSE_ERROR;
      }

      api->skip_newlines(parser);
      if (!api->expect(parser, PIE_TOK_COLON,
                       "expected ':' after field name")) {
        free(field_name);
        pie_expr_free(expr);
        return PIE_PARSE_ERROR;
      }

      api->skip_newlines(parser);
      PieExpr *value = api->parse_expr(parser);
      if (!value) {
        free(field_name);
        pie_expr_free(expr);
        return PIE_PARSE_ERROR;
      }

      if (!pie_expr_new_add_arg(expr, field_name, value)) {
        free(field_name);
        pie_expr_free(value);
        pie_expr_free(expr);
        return PIE_PARSE_ERROR;
      }
      free(field_name);

      api->skip_newlines(parser);
      if (api->match(parser, PIE_TOK_COMMA)) {
        continue;
      }
      break;
    }
  }

  api->skip_newlines(parser);
  if (!api->expect(parser, PIE_TOK_RPAREN,
                   "expected ')' after new arguments")) {
    pie_expr_free(expr);
    return PIE_PARSE_ERROR;
  }

  *out_expr = expr;
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_structs_parse_field_expr(PieParseContext *ctx,
                                                    PieExpr **left,
                                                    int min_precedence) {
  (void)min_precedence;
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->check(parser, PIE_TOK_DOT)) {
    return PIE_PARSE_NO_MATCH;
  }
  api->advance(parser);

  if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
    api->error_at(parser, api->peek(parser),
                  "expected field name or method name after '.'");
    return PIE_PARSE_ERROR;
  }

  if (api->peek_n(parser, 1)->kind == PIE_TOK_LPAREN) {
    const PieToken *method_token = api->advance(parser);
    char *method_name = api->copy_token_text(method_token);
    if (!method_name) {
      pie_diag_error(api->diag(parser), "out of memory");
      return PIE_PARSE_ERROR;
    }

    api->advance(parser);

    PieExpr *call = pie_expr_method_call(*left, method_name);
    free(method_name);
    if (!call) {
      pie_diag_error(api->diag(parser), "out of memory");
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
        if (!pie_expr_method_call_add_arg(call, arg)) {
          pie_expr_free(arg);
          pie_expr_free(call);
          pie_diag_error(api->diag(parser), "out of memory");
          return PIE_PARSE_ERROR;
        }
        api->skip_newlines(parser);
      } while (api->match(parser, PIE_TOK_COMMA));
    }

    if (!api->expect(parser, PIE_TOK_RPAREN,
                     "expected ')' after method arguments")) {
      pie_expr_free(call);
      return PIE_PARSE_ERROR;
    }

    *left = call;
    return PIE_PARSE_OK;
  }

  const PieToken *field_token = api->advance(parser);
  char *field_name = api->copy_token_text(field_token);
  if (!field_name) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  *left = pie_expr_field(*left, field_name);
  free(field_name);
  if (!*left) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}

PieParseResult
pie_feature_structs_parse_field_assign_stmt(PieParseContext *ctx,
                                            PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  size_t start = api->pos(parser);

  if (!api->check(parser, PIE_TOK_IDENTIFIER) &&
      !api->check(parser, PIE_TOK_SELF)) {
    return PIE_PARSE_NO_MATCH;
  }
  const PieToken *name_token = api->peek(parser);
  api->advance(parser);

  if (!api->check(parser, PIE_TOK_DOT)) {
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }
  api->advance(parser);

  if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }
  const PieToken *field_token = api->advance(parser);
  char *field_name = api->copy_token_text(field_token);

  api->skip_separators(parser);

  if (!api->check(parser, PIE_TOK_EQ) && !api->check(parser, PIE_TOK_PLUS_EQ) &&
      !api->check(parser, PIE_TOK_MINUS_EQ) &&
      !api->check(parser, PIE_TOK_STAR_EQ) &&
      !api->check(parser, PIE_TOK_SLASH_EQ) &&
      !api->check(parser, PIE_TOK_PERCENT_EQ)) {
    free(field_name);
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }
  const PieToken *op_token = api->advance(parser);
  char assign_op[4] = {0};
  if (op_token->len < 4) {
    memcpy(assign_op, op_token->start, op_token->len);
  }

  api->skip_separators(parser);

  size_t stmt_end = api->find_stmt_end(parser, api->pos(parser));
  PieExpr *value = api->parse_expr_until(parser, stmt_end);
  if (!value) {
    free(field_name);
    return PIE_PARSE_ERROR;
  }

  char *target_name = api->copy_token_text(name_token);
  PieExpr *target = pie_expr_var(target_name);
  free(target_name);
  PieExpr *field_target = pie_expr_field(target, field_name);
  free(field_name);

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_FIELD_ASSIGN;
  stmt.field_target = field_target;
  stmt.expr = value;
  strncpy(stmt.assign_op, assign_op, sizeof(stmt.assign_op) - 1);
  if (!pie_program_push_stmt(program, stmt)) {
    pie_expr_free(field_target);
    pie_expr_free(value);
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
