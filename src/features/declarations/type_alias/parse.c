#include "pie/core/ast/ast.h"
#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

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
  if (api->check(parser, PIE_TOK_VOID_TYPE)) {
    api->advance(parser);
    return pie_ast_type_simple(PIE_AST_TYPE_VOID);
  }

  if (api->check(parser, PIE_TOK_LIST_TYPE)) {
    api->advance(parser);
    if (!api->expect(parser, PIE_TOK_LPAREN, "expected '(' after 'list'")) {
      return pie_ast_type_simple(PIE_AST_TYPE_INFER);
    }
    PieAstType inner = parse_type_annotation(ctx);
    if (!api->expect(parser, PIE_TOK_RPAREN,
                     "expected ')' after list element type")) {
      return pie_ast_type_simple(PIE_AST_TYPE_INFER);
    }
    return pie_ast_type_list(inner.kind, inner.width);
  }

  if (api->check(parser, PIE_TOK_IDENTIFIER)) {
    const PieToken *tok = api->advance(parser);
    PieAstType type = pie_ast_type_simple(PIE_AST_TYPE_STRUCT);
    type.struct_name = api->copy_token_text(tok);
    return type;
  }

  api->error_at(parser, api->peek(parser), "expected type name");
  return pie_ast_type_simple(PIE_AST_TYPE_INFER);
}

PieParseResult pie_feature_type_alias_parse_top_level(PieParseContext *ctx,
                                                      PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_TYPE)) {
    return PIE_PARSE_NO_MATCH;
  }

  api->skip_separators(parser);

  if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
    api->error_at(parser, api->peek(parser), "expected type alias name");
    return PIE_PARSE_ERROR;
  }
  const PieToken *name_token = api->advance(parser);
  char *alias_name = api->copy_token_text(name_token);
  if (!alias_name) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_COLON,
                   "expected ':' after type alias name")) {
    free(alias_name);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);
  PieAstType aliased_type = parse_type_annotation(ctx);
  if (aliased_type.kind == PIE_AST_TYPE_INFER) {
    free(alias_name);
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_TYPE_ALIAS;
  stmt.name = alias_name;
  stmt.type_annotation = aliased_type;

  if (!pie_program_push_stmt(program, stmt)) {
    free(alias_name);
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  return PIE_PARSE_OK;
}
