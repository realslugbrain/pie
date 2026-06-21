#include "pie/core/lower/lower.h"
#include "pie/core/ast/ast.h"

PieLowerResult pie_feature_type_alias_lower_stmt(PieLowerContext *ctx,
                                                 const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_TYPE_ALIAS) {
    return PIE_LOWER_NO_MATCH;
  }

  return PIE_LOWER_OK;
}
