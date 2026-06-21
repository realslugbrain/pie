#ifndef PIE_CORE_PARSER_PARSER_H
#define PIE_CORE_PARSER_PARSER_H

#include "pie/core/ast/ast.h"
#include "pie/core/diag/diag.h"
#include "pie/core/source/source.h"
#include "pie/core/token/token.h"

typedef struct PieParser PieParser;

typedef enum PieParseResult {
  PIE_PARSE_ERROR = -1,
  PIE_PARSE_NO_MATCH = 0,
  PIE_PARSE_OK = 1
} PieParseResult;

typedef struct PieParserApi {
  const PieToken *(*peek)(PieParser *parser);
  const PieToken *(*peek_n)(PieParser *parser, size_t n);
  const PieToken *(*advance)(PieParser *parser);
  int (*check)(PieParser *parser, PieTokenKind kind);
  int (*match)(PieParser *parser, PieTokenKind kind);
  int (*expect)(PieParser *parser, PieTokenKind kind, const char *message);
  void (*skip_separators)(PieParser *parser);
  void (*skip_newlines)(PieParser *parser);
  void (*skip_to_stmt_end)(PieParser *parser);
  size_t (*find_stmt_end)(PieParser *parser, size_t start);
  size_t (*pos)(PieParser *parser);
  void (*set_pos)(PieParser *parser, size_t pos);
  PieDiagnosticBag *(*diag)(PieParser *parser);
  char *(*copy_token_text)(const PieToken *token);
  void (*error_at)(PieParser *parser, const PieToken *token,
                   const char *message);
  PieExpr *(*parse_expr)(PieParser *parser);
  PieExpr *(*parse_expr_prec)(PieParser *parser, int min_precedence);
  PieExpr *(*parse_expr_until)(PieParser *parser, size_t end);
  int (*parse_statement)(PieParser *parser, PieProgram *program);
  PieExpr *(*parse_expr_from_text)(PieParser *parser, const char *text,
                                   size_t len);
} PieParserApi;

typedef struct PieParseContext {
  PieParser *parser;
  const PieParserApi *api;
} PieParseContext;

typedef PieParseResult (*PieTopLevelParseFn)(PieParseContext *ctx,
                                             PieProgram *program);
typedef PieParseResult (*PieStmtParseFn)(PieParseContext *ctx,
                                         PieProgram *program);
typedef PieParseResult (*PieExprPrefixParseFn)(PieParseContext *ctx,
                                               PieExpr **out_expr);
typedef PieParseResult (*PieExprInfixParseFn)(PieParseContext *ctx,
                                              PieExpr **left,
                                              int min_precedence);

typedef struct PieTopLevelParseHook {
  const char *feature_id;
  PieTopLevelParseFn parse;
} PieTopLevelParseHook;

typedef struct PieStmtParseHook {
  const char *feature_id;
  PieStmtParseFn parse;
} PieStmtParseHook;

typedef struct PieExprPrefixParseHook {
  const char *feature_id;
  PieExprPrefixParseFn parse;
} PieExprPrefixParseHook;

typedef struct PieExprInfixParseHook {
  const char *feature_id;
  PieExprInfixParseFn parse;
} PieExprInfixParseHook;

typedef struct PieParseHookRegistry {
  const PieTopLevelParseHook *top_level_hooks;
  size_t top_level_hook_count;
  const PieStmtParseHook *stmt_hooks;
  size_t stmt_hook_count;
  const PieExprPrefixParseHook *expr_prefix_hooks;
  size_t expr_prefix_hook_count;
  const PieExprInfixParseHook *expr_infix_hooks;
  size_t expr_infix_hook_count;
} PieParseHookRegistry;

const PieParseHookRegistry *pie_parse_hook_registry(void);

int pie_parse_source(const PieSource *source, PieProgram *program,
                     PieDiagnosticBag *diag);

#endif
