#include "pie/core/lower/lower.h"
#include "pie/core/ast/ast.h"
#include "pie/core/ir/ir.h"
#include "pie/core/parser/parser.h"
#include "pie/core/type_width.h"

#include <stdlib.h>
#include <string.h>

static PieIrExpr *make_string_ir(const char *text, size_t len) {
  return pie_ir_expr_string(text, len);
}

static PieIrExpr *ensure_string_ir(PieLowerContext *ctx, PieIrExpr *ir,
                                   PieIrTypeKind type_kind) {
  (void)ctx;
  if (type_kind == PIE_IR_TYPE_STRING) {
    return ir;
  }

  PieIrExpr *cast = pie_ir_expr_cast(ir, PIE_IR_TYPE_STRING, PIE_WIDTH_64);
  if (!cast) {
    pie_ir_expr_free(ir);
    return NULL;
  }
  return cast;
}

static PieIrExpr *build_concat_ir(PieLowerContext *ctx, PieExpr **interp_exprs,
                                  char **interp_texts, size_t *interp_text_lens,
                                  size_t part_count) {
  PieIrExpr *result = NULL;

  for (size_t i = 0; i < part_count; i++) {
    if (interp_texts[i] && interp_text_lens[i] > 0) {
      PieIrExpr *str_part =
          make_string_ir(interp_texts[i], interp_text_lens[i]);
      if (!str_part) {
        pie_ir_expr_free(result);
        return NULL;
      }
      if (result) {
        PieIrExpr *concat = pie_ir_expr_binary_typed("++", result, str_part,
                                                     PIE_IR_TYPE_STRING);
        if (!concat) {
          pie_ir_expr_free(result);
          pie_ir_expr_free(str_part);
          return NULL;
        }
        result = concat;
      } else {
        result = str_part;
      }
    }

    if (interp_exprs[i]) {
      PieIrExpr *sub_ir = NULL;
      if (ctx->api->lower_expr(ctx->lower, interp_exprs[i], &sub_ir) !=
          PIE_LOWER_OK) {
        pie_ir_expr_free(result);
        return NULL;
      }

      sub_ir = ensure_string_ir(ctx, sub_ir, sub_ir->type);
      if (!sub_ir) {
        pie_ir_expr_free(result);
        return NULL;
      }

      if (result) {
        PieIrExpr *concat =
            pie_ir_expr_binary_typed("++", result, sub_ir, PIE_IR_TYPE_STRING);
        if (!concat) {
          pie_ir_expr_free(result);
          pie_ir_expr_free(sub_ir);
          return NULL;
        }
        result = concat;
      } else {
        result = sub_ir;
      }
    }
  }

  return result;
}

PieLowerResult pie_feature_string_interp_lower_expr(PieLowerContext *ctx,
                                                    const PieExpr *expr,
                                                    PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_STRING_INTERP) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *concat =
      build_concat_ir(ctx, expr->interp_exprs, expr->interp_texts,
                      expr->interp_text_lens, expr->interp_part_count);

  if (!concat) {
    ctx->api->error(ctx->lower,
                    "out of memory while building string interpolation");
    return PIE_LOWER_ERROR;
  }

  *out_expr = concat;
  return PIE_LOWER_OK;
}
