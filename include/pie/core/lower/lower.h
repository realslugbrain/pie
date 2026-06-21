#ifndef PIE_CORE_LOWER_LOWER_H
#define PIE_CORE_LOWER_LOWER_H

#include <stddef.h>

#include "pie/core/ast/ast.h"
#include "pie/core/diag/diag.h"
#include "pie/core/ir/ir.h"

typedef struct PieLower PieLower;

typedef enum PieLowerResult {
  PIE_LOWER_ERROR = -1,
  PIE_LOWER_NO_MATCH = 0,
  PIE_LOWER_OK = 1
} PieLowerResult;

typedef struct PieLowerFunctionInfo {
  PieIrTypeKind return_type;
  int return_type_width;
  PieIrTypeKind return_raw_pointee_type;
  int return_raw_pointee_width;
  PieIrTypeKind return_ref_inner_type;
  int return_ref_inner_width;
  const PieIrTypeKind *param_types;
  const int *param_type_widths;
  const PieIrTypeKind *param_raw_pointee_types;
  const int *param_raw_pointee_widths;
  size_t param_count;
  const char **param_names;
  const char **type_param_names;
  size_t type_param_count;
} PieLowerFunctionInfo;

typedef struct PieLowerApi {
  PieIrProgram *(*ir)(PieLower *lower);
  int (*declare_local)(PieLower *lower, const char *name, int is_mut,
                       PieIrTypeKind type, int type_width,
                       PieIrTypeKind raw_pointee_type, int raw_pointee_width,
                       PieIrTypeKind ref_inner_type, int ref_inner_width,
                       const char *struct_name, const char *enum_name,
                       size_t *out_id);
  int (*find_local)(PieLower *lower, const char *name, size_t *out_id,
                    int *out_is_mut, PieIrTypeKind *out_type,
                    int *out_type_width, PieIrTypeKind *out_raw_pointee_type,
                    int *out_raw_pointee_width,
                    PieIrTypeKind *out_ref_inner_type, int *out_ref_inner_width,
                    const char **out_struct_name, const char **out_enum_name);
  int (*find_function)(PieLower *lower, const char *name,
                       PieLowerFunctionInfo *out_function);
  const PieStructDef *(*find_struct)(PieLower *lower, const char *name);
  const PieEnumDef *(*find_enum)(PieLower *lower, const char *name);
  int (*find_variant_tag)(PieLower *lower, const char *enum_name,
                          const char *variant_name, int *out_tag);
  const PieProgram *(*program)(PieLower *lower);
  PieIrTypeKind (*current_return_type)(PieLower *lower);
  int (*push_stmt)(PieLower *lower, PieIrStmt stmt);
  PieLowerResult (*lower_expr)(PieLower *lower, const PieExpr *expr,
                               PieIrExpr **out_expr);
  PieLowerResult (*lower_stmt)(PieLower *lower, const PieStmt *stmt);
  int (*lower_block)(PieLower *lower, const PieProgram *program,
                     PieIrProgram *out_ir);
  int (*enter_scope)(PieLower *lower);
  void (*leave_scope)(PieLower *lower);
  void (*error)(PieLower *lower, const char *message);
  void (*errorf)(PieLower *lower, const char *fmt, ...);
  PieDiagnosticBag *(*diag)(PieLower *lower);
  int (*find_capture)(PieLower *lower, const char *name,
                      PieIrTypeKind *out_type, int *out_type_width,
                      size_t *out_env_offset);
  void (*set_closure_captures)(PieLower *lower, char **names,
                               PieIrTypeKind *types, size_t count);
  int (*declare_capture)(PieLower *lower, const char *name, int is_mut,
                         PieIrTypeKind type, int type_width, size_t local_id);
  void (*set_current_ir)(PieLower *lower, PieIrProgram *ir);
  PieIrProgram *(*current_ir)(PieLower *lower);
  void (*set_root_ir)(PieLower *lower, PieIrProgram *ir);
  PieIrProgram *(*root_ir)(PieLower *lower);
  void (*set_current_body)(PieLower *lower, const PieProgram *body);
  const PieProgram *(*current_body)(PieLower *lower);
} PieLowerApi;

typedef struct PieLowerContext {
  PieLower *lower;
  const PieLowerApi *api;
} PieLowerContext;

typedef PieLowerResult (*PieLowerStmtFn)(PieLowerContext *ctx,
                                         const PieStmt *stmt);
typedef PieLowerResult (*PieLowerExprFn)(PieLowerContext *ctx,
                                         const PieExpr *expr,
                                         PieIrExpr **out_expr);

typedef struct PieLowerStmtHook {
  const char *feature_id;
  PieLowerStmtFn lower;
} PieLowerStmtHook;

typedef struct PieLowerExprHook {
  const char *feature_id;
  PieLowerExprFn lower;
} PieLowerExprHook;

typedef struct PieLowerHookRegistry {
  const PieLowerStmtHook *stmt_hooks;
  size_t stmt_hook_count;
  const PieLowerExprHook *expr_hooks;
  size_t expr_hook_count;
} PieLowerHookRegistry;

const PieLowerHookRegistry *pie_lower_hook_registry(void);
int pie_lower_program(const PieProgram *program, PieIrProgram *out_ir,
                      PieDiagnosticBag *diag);

#endif
