#include "pie/core/ast/ast.h"
#include "pie/core/parser/parser.h"

#include <stdlib.h>

static PieAstType parse_type_annotation(PieParseContext *ctx) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (api->check(parser, PIE_TOK_INT_TYPE)) {
    api->advance(parser);
    return pie_ast_type_simple(PIE_AST_TYPE_INT);
  }
  if (api->check(parser, PIE_TOK_FLOAT_TYPE)) {
    api->advance(parser);
    return pie_ast_type_simple(PIE_AST_TYPE_FLOAT);
  }
  if (api->check(parser, PIE_TOK_BOOL_TYPE)) {
    api->advance(parser);
    return pie_ast_type_simple(PIE_AST_TYPE_BOOL);
  }
  if (api->check(parser, PIE_TOK_STRING_TYPE)) {
    api->advance(parser);
    return pie_ast_type_simple(PIE_AST_TYPE_STRING);
  }
  if (api->check(parser, PIE_TOK_CHAR_TYPE)) {
    api->advance(parser);
    return pie_ast_type_simple(PIE_AST_TYPE_CHAR);
  }
  if (api->check(parser, PIE_TOK_BYTE_TYPE)) {
    api->advance(parser);
    return pie_ast_type_simple(PIE_AST_TYPE_BYTE);
  }

  api->error_at(parser, api->peek(parser),
                "expected type name (int, float, bool, string, char, byte)");
  return pie_ast_type_simple(PIE_AST_TYPE_INFER);
}

PieParseResult pie_feature_const_parse_top_level(PieParseContext *ctx,
                                                 PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_CONST)) {
    return PIE_PARSE_NO_MATCH;
  }

  api->skip_separators(parser);

  if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
    api->error_at(parser, api->peek(parser), "expected constant name");
    return PIE_PARSE_ERROR;
  }
  const PieToken *name_token = api->advance(parser);
  char *const_name = api->copy_token_text(name_token);
  if (!const_name) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after constant name")) {
    free(const_name);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);
  PieAstType type = parse_type_annotation(ctx);
  if (type.kind == PIE_AST_TYPE_INFER) {
    free(const_name);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_EQ,
                   "expected '=' after type in const declaration")) {
    free(const_name);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);

  PieExpr *value = api->parse_expr(parser);
  if (!value) {
    free(const_name);
    return PIE_PARSE_ERROR;
  }

  PieConstDef *def = (PieConstDef *)malloc(sizeof(PieConstDef));
  if (!def) {
    free(const_name);
    pie_expr_free(value);
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }
  def->name = const_name;
  def->type = type;
  def->value = value;

  if (!pie_program_push_const(program, *def)) {
    free(def->name);
    pie_expr_free(def->value);
    free(def);
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_CONST;
  stmt.const_def = def;

  if (!pie_program_push_stmt(program, stmt)) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  return PIE_PARSE_OK;
}
