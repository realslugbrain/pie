#ifndef PIE_BACKEND_ASM_ASM_CODEGEN_H
#define PIE_BACKEND_ASM_ASM_CODEGEN_H

#include <stddef.h>

#include "pie/core/diag/diag.h"
#include "pie/core/ir/ir.h"

typedef struct PieAsmCodegen PieAsmCodegen;

typedef enum PieAsmGenResult {
  PIE_ASM_GEN_ERROR = -1,
  PIE_ASM_GEN_NO_MATCH = 0,
  PIE_ASM_GEN_OK = 1
} PieAsmGenResult;

typedef struct PieAsmSymbol {
  int offset;
  int is_mut;
  PieIrTypeKind type;
  int type_width;
  PieIrTypeKind ref_inner_type;
  int ref_inner_width;
} PieAsmSymbol;

typedef struct PieAsmCodegenApi {
  void (*emit)(PieAsmCodegen *cg, const char *fmt, ...);
  int (*emit_expr)(PieAsmCodegen *cg, const PieIrExpr *expr);
  int (*emit_stmt)(PieAsmCodegen *cg, const PieIrStmt *stmt);
  int (*add_local)(PieAsmCodegen *cg, size_t local_id, int is_mut,
                   PieIrTypeKind type, int type_width,
                   PieIrTypeKind ref_inner_type, int ref_inner_width,
                   PieAsmSymbol *out_symbol);
  int (*find_local)(PieAsmCodegen *cg, size_t local_id,
                    PieAsmSymbol *out_symbol);
  int (*add_local_at)(PieAsmCodegen *cg, size_t local_id, int offset,
                      int is_mut, PieIrTypeKind type, int type_width,
                      PieAsmSymbol *out_symbol);
  int (*add_string)(PieAsmCodegen *cg, const char *bytes, size_t len,
                    size_t *out_id);
  void (*error)(PieAsmCodegen *cg, const char *message);
  void (*errorf)(PieAsmCodegen *cg, const char *fmt, ...);
  PieDiagnosticBag *(*diag)(PieAsmCodegen *cg);
  void (*mark_returned)(PieAsmCodegen *cg);
  int (*next_label)(PieAsmCodegen *cg);
  int (*push_loop)(PieAsmCodegen *cg, int continue_label, int break_label,
                   const char *label_name);
  void (*pop_loop)(PieAsmCodegen *cg);
  int (*current_loop)(PieAsmCodegen *cg, int *out_continue_label,
                      int *out_break_label);
  int (*find_loop_label)(PieAsmCodegen *cg, const char *label_name,
                         int *out_continue_label, int *out_break_label);
  int (*find_capture)(PieAsmCodegen *cg, const char *name, int *out_offset);
  void (*set_closure_context)(PieAsmCodegen *cg, int in_closure, char **names,
                              size_t count);
  int (*in_closure_body)(PieAsmCodegen *cg);
  size_t (*closure_capture_count)(PieAsmCodegen *cg);
  const char *(*closure_capture_name)(PieAsmCodegen *cg, size_t index);
  void (*push_frame)(PieAsmCodegen *cg);
  void (*pop_frame)(PieAsmCodegen *cg);
  void (*set_stack_offset)(PieAsmCodegen *cg, int offset);
  int (*emit_deferred)(PieAsmCodegen *cg);
} PieAsmCodegenApi;

typedef struct PieAsmCodegenContext {
  PieAsmCodegen *cg;
  const PieAsmCodegenApi *api;
} PieAsmCodegenContext;

typedef PieAsmGenResult (*PieAsmStmtGenFn)(PieAsmCodegenContext *ctx,
                                           const PieIrStmt *stmt);
typedef PieAsmGenResult (*PieAsmExprGenFn)(PieAsmCodegenContext *ctx,
                                           const PieIrExpr *expr);

typedef struct PieAsmStmtGenHook {
  const char *feature_id;
  PieAsmStmtGenFn generate;
} PieAsmStmtGenHook;

typedef struct PieAsmExprGenHook {
  const char *feature_id;
  PieAsmExprGenFn generate;
} PieAsmExprGenHook;

typedef struct PieAsmHookRegistry {
  const PieAsmStmtGenHook *stmt_hooks;
  size_t stmt_hook_count;
  const PieAsmExprGenHook *expr_hooks;
  size_t expr_hook_count;
} PieAsmHookRegistry;

const PieAsmHookRegistry *pie_asm_hook_registry(void);

#endif
