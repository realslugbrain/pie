#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

static void free_partial_print_stmt(PieIrStmt *stmt) {
  for (size_t i = 0; i < stmt->arg_count; i++) {
    free(stmt->args[i].text);
    pie_ir_expr_free(stmt->args[i].expr);
  }
  free(stmt->args);
  stmt->args = NULL;
  stmt->arg_count = 0;
}

static int push_arg(PieIrStmt *stmt, PieIrPrintArg arg) {
  PieIrPrintArg *next = (PieIrPrintArg *)realloc(
      stmt->args, (stmt->arg_count + 1) * sizeof(PieIrPrintArg));
  if (!next) {
    return 0;
  }
  stmt->args = next;
  stmt->args[stmt->arg_count++] = arg;
  return 1;
}

PieLowerResult pie_feature_print_lower_stmt(PieLowerContext *ctx,
                                            const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_PRINT) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrStmt ir_stmt;
  memset(&ir_stmt, 0, sizeof(ir_stmt));
  ir_stmt.kind = PIE_IR_STMT_PRINT;
  ir_stmt.println = stmt->println;

  for (size_t i = 0; i < stmt->arg_count; i++) {
    PieIrPrintArg arg;
    memset(&arg, 0, sizeof(arg));

    if (stmt->args[i].is_string) {
      arg.is_string = 1;
      arg.text =
          (char *)malloc(stmt->args[i].text_len ? stmt->args[i].text_len : 1);
      if (!arg.text) {
        free_partial_print_stmt(&ir_stmt);
        ctx->api->error(ctx->lower,
                        "out of memory while lowering print string");
        return PIE_LOWER_ERROR;
      }
      if (stmt->args[i].text_len) {
        memcpy(arg.text, stmt->args[i].text, stmt->args[i].text_len);
      }
      arg.text_len = stmt->args[i].text_len;
    } else {
      if (ctx->api->lower_expr(ctx->lower, stmt->args[i].expr, &arg.expr) !=
          PIE_LOWER_OK) {
        free_partial_print_stmt(&ir_stmt);
        return PIE_LOWER_ERROR;
      }
    }

    if (!push_arg(&ir_stmt, arg)) {
      free(arg.text);
      pie_ir_expr_free(arg.expr);
      free_partial_print_stmt(&ir_stmt);
      ctx->api->error(ctx->lower,
                      "out of memory while lowering print argument");
      return PIE_LOWER_ERROR;
    }
  }

  if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
    free_partial_print_stmt(&ir_stmt);
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
