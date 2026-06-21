#include "pie/core/sema/sema.h"

PieSemaResult pie_feature_pass_sema_stmt(PieSemaContext *ctx,
                                         const PieStmt *stmt) {
  (void)ctx;
  if (stmt->kind != PIE_STMT_PASS) {
    return PIE_SEMA_NO_MATCH;
  }
  return PIE_SEMA_OK;
}
