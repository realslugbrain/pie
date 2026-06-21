#include "pie/core/sema/sema.h"
#include "pie/core/ast/ast.h"

#include <string.h>

PieSemaResult pie_feature_type_alias_sema_stmt(PieSemaContext *ctx,
                                               const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_TYPE_ALIAS) {
    return PIE_SEMA_NO_MATCH;
  }

  return PIE_SEMA_OK;
}
