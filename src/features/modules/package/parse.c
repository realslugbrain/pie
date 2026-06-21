#include "pie/core/parser/parser.h"

PieParseResult
pie_feature_modules_package_parse_top_level(PieParseContext *ctx,
                                            PieProgram *program) {
  (void)program;
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->check(parser, PIE_TOK_PACKAGE)) {
    return PIE_PARSE_NO_MATCH;
  }

  api->skip_to_stmt_end(parser);
  return PIE_PARSE_OK;
}
