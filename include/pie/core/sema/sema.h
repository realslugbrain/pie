#ifndef PIE_CORE_SEMA_SEMA_H
#define PIE_CORE_SEMA_SEMA_H

#include <stddef.h>

#include "pie/core/ast/ast.h"
#include "pie/core/diag/diag.h"

typedef struct PieSema PieSema;

typedef enum PieSemaResult {
  PIE_SEMA_ERROR = -1,
  PIE_SEMA_NO_MATCH = 0,
  PIE_SEMA_OK = 1
} PieSemaResult;

typedef enum PieTypeKind {
  PIE_TYPE_ERROR = 0,
  PIE_TYPE_VOID,
  PIE_TYPE_INT,
  PIE_TYPE_FLOAT,
  PIE_TYPE_CHAR,
  PIE_TYPE_BYTE,
  PIE_TYPE_BOOL,
  PIE_TYPE_STRING,
  PIE_TYPE_REF,
  PIE_TYPE_REF_MUT,
  PIE_TYPE_RAW_PTR,
  PIE_TYPE_STRUCT,
  PIE_TYPE_NULL,
  PIE_TYPE_NULLABLE,
  PIE_TYPE_TUPLE,
  PIE_TYPE_LIST,
  PIE_TYPE_MAP,
  PIE_TYPE_ENUM,
  PIE_TYPE_CLOSURE,
  PIE_TYPE_THREAD,
  PIE_TYPE_MUTEX,
  PIE_TYPE_CHANNEL
} PieTypeKind;

typedef struct PieType {
  PieTypeKind kind;
  int type_width;
  PieTypeKind raw_pointee_kind;
  int raw_pointee_width;
  PieTypeKind nullable_inner_kind;
  int nullable_inner_width;
  PieTypeKind ref_inner_kind;
  int ref_inner_width;
  char *ref_inner_struct_name;
  char *struct_name;
  PieTypeKind tuple_element_kinds[8];
  int tuple_element_widths[8];
  size_t tuple_element_count;
  PieTypeKind list_element_kind;
  int list_element_width;
  PieTypeKind map_key_kind;
  int map_key_width;
  PieTypeKind map_value_kind;
  int map_value_width;
  char *enum_name;
  PieTypeKind *func_param_kinds;
  int *func_param_widths;
  size_t func_param_count;
  PieTypeKind func_return_kind;
  int func_return_width;
} PieType;

typedef struct PieSymbolInfo {
  PieType type;
  int is_mut;
} PieSymbolInfo;

typedef struct PieFunctionInfo {
  PieType return_type;
  const PieType *param_types;
  size_t param_count;
  int is_unsafe;
  const char **type_param_names;
  size_t type_param_count;
  const char **type_param_constraints;
} PieFunctionInfo;

typedef struct PieSemaApi {
  int (*declare_symbol)(PieSema *sema, const char *name, PieType type,
                        int is_mut);
  int (*find_symbol)(PieSema *sema, const char *name,
                     PieSymbolInfo *out_symbol);
  int (*find_function)(PieSema *sema, const char *name,
                       PieFunctionInfo *out_function);
  const PieStructDef *(*find_struct)(PieSema *sema, const char *name);
  const PieEnumDef *(*find_enum)(PieSema *sema, const char *name);
  PieType (*current_return_type)(PieSema *sema);
  void (*set_return_type)(PieSema *sema, PieType type);
  PieSemaResult (*check_expr)(PieSema *sema, const PieExpr *expr,
                              PieType *out_type);
  PieSemaResult (*check_stmt)(PieSema *sema, const PieStmt *stmt);
  PieSemaResult (*check_block)(PieSema *sema, const PieProgram *program);
  int (*enter_scope)(PieSema *sema);
  void (*leave_scope)(PieSema *sema);
  void (*enter_loop)(PieSema *sema);
  void (*leave_loop)(PieSema *sema);
  int (*in_loop)(PieSema *sema);
  void (*enter_unsafe)(PieSema *sema);
  void (*leave_unsafe)(PieSema *sema);
  int (*in_unsafe)(PieSema *sema);
  void (*error)(PieSema *sema, const char *message);
  void (*errorf)(PieSema *sema, const char *fmt, ...);
  PieDiagnosticBag *(*diag)(PieSema *sema);
  const char *(*type_name)(PieType type);
  const PieProgram *(*program)(PieSema *sema);
  int (*push_pending_mono)(PieSema *sema, PieFunction func);
  int (*register_mono_func)(PieSema *sema, const char *name,
                            PieType return_type, PieType *param_types,
                            size_t param_count);
} PieSemaApi;

typedef struct PieSemaContext {
  PieSema *sema;
  const PieSemaApi *api;
} PieSemaContext;

typedef PieSemaResult (*PieSemaStmtFn)(PieSemaContext *ctx,
                                       const PieStmt *stmt);
typedef PieSemaResult (*PieSemaExprFn)(PieSemaContext *ctx, const PieExpr *expr,
                                       PieType *out_type);

typedef struct PieSemaStmtHook {
  const char *feature_id;
  PieSemaStmtFn check;
} PieSemaStmtHook;

typedef struct PieSemaExprHook {
  const char *feature_id;
  PieSemaExprFn check;
} PieSemaExprHook;

typedef struct PieSemaHookRegistry {
  const PieSemaStmtHook *stmt_hooks;
  size_t stmt_hook_count;
  const PieSemaExprHook *expr_hooks;
  size_t expr_hook_count;
} PieSemaHookRegistry;

const PieSemaHookRegistry *pie_sema_hook_registry(void);
int pie_sema_program(const PieProgram *program, PieDiagnosticBag *diag);

#endif
