#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

static int is_std_like_path(const char *path) {
  return strchr(path, '/') == NULL || strncmp(path, "wasm/", 5) == 0;
}

PieParseResult
pie_feature_modules_require_parse_top_level(PieParseContext *ctx,
                                            PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (api->check(parser, PIE_TOK_FROM)) {
    api->advance(parser);
    const PieToken *path_token = api->peek(parser);
    if (!api->expect(parser, PIE_TOK_STRING_LITERAL,
                     "expected string path after 'from'")) {
      return PIE_PARSE_ERROR;
    }

    char *path = (char *)malloc(path_token->string_len + 1);
    if (!path) {
      pie_diag_error(api->diag(parser),
                     "out of memory while parsing from path");
      return PIE_PARSE_ERROR;
    }
    if (path_token->string_len) {
      memcpy(path, path_token->string_value, path_token->string_len);
    }
    path[path_token->string_len] = '\0';

    if (path[0] == '\0') {
      free(path);
      api->error_at(parser, path_token, "from path cannot be empty");
      return PIE_PARSE_ERROR;
    }

    if (!pie_program_push_require(
            program, path,
            is_std_like_path(path) ? PIE_REQUIRE_STD : PIE_REQUIRE_PACKAGE)) {
      free(path);
      pie_diag_error(api->diag(parser),
                     "out of memory while storing from path");
      return PIE_PARSE_ERROR;
    }
    free(path);

    if (!api->expect(parser, PIE_TOK_IMPORT,
                     "expected 'import' after from path")) {
      return PIE_PARSE_ERROR;
    }

    for (;;) {
      const PieToken *name_tok = api->peek(parser);
      if (name_tok->kind != PIE_TOK_IDENTIFIER) {
        break;
      }
      api->advance(parser);
      if (api->check(parser, PIE_TOK_AS)) {
        api->advance(parser);
        const PieToken *alias_tok = api->peek(parser);
        if (alias_tok->kind != PIE_TOK_IDENTIFIER) {
          api->error_at(parser, alias_tok, "expected identifier after 'as'");
          return PIE_PARSE_ERROR;
        }
        api->advance(parser);
      }
      if (!api->check(parser, PIE_TOK_COMMA)) {
        break;
      }
      api->advance(parser);
    }

    api->skip_to_stmt_end(parser);
    return PIE_PARSE_OK;
  }

  if (!api->check(parser, PIE_TOK_REQUIRE)) {
    if (api->check(parser, PIE_TOK_IMPORT)) {
      api->skip_to_stmt_end(parser);
      return PIE_PARSE_OK;
    }
    return PIE_PARSE_NO_MATCH;
  }

  api->advance(parser);
  const PieToken *path_token = api->peek(parser);
  if (!api->expect(parser, PIE_TOK_STRING_LITERAL,
                   "expected string path after require")) {
    return PIE_PARSE_ERROR;
  }

  char *path = (char *)malloc(path_token->string_len + 1);
  if (!path) {
    pie_diag_error(api->diag(parser),
                   "out of memory while parsing require path");
    return PIE_PARSE_ERROR;
  }
  if (path_token->string_len) {
    memcpy(path, path_token->string_value, path_token->string_len);
  }
  path[path_token->string_len] = '\0';

  if (path[0] == '\0') {
    free(path);
    api->error_at(parser, path_token, "require path cannot be empty");
    return PIE_PARSE_ERROR;
  }
  if (!pie_program_push_require(program, path,
                                is_std_like_path(path) ? PIE_REQUIRE_STD
                                                       : PIE_REQUIRE_PACKAGE)) {
    free(path);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing require path");
    return PIE_PARSE_ERROR;
  }
  free(path);

  api->skip_to_stmt_end(parser);
  return PIE_PARSE_OK;
}
