#ifndef PIE_CORE_AST_AST_H
#define PIE_CORE_AST_AST_H

#include <stddef.h>
#include <string.h>

#include "pie/core/type_width.h"

typedef enum PieExprKind {
  PIE_EXPR_INT,
  PIE_EXPR_FLOAT,
  PIE_EXPR_CHAR,
  PIE_EXPR_BOOL,
  PIE_EXPR_STRING,
  PIE_EXPR_STRING_INTERP,
  PIE_EXPR_VAR,
  PIE_EXPR_CALL,
  PIE_EXPR_BINARY,
  PIE_EXPR_UNARY,
  PIE_EXPR_NEW,
  PIE_EXPR_FIELD,
  PIE_EXPR_NULL,
  PIE_EXPR_TUPLE,
  PIE_EXPR_LIST,
  PIE_EXPR_INDEX,
  PIE_EXPR_VARIANT,
  PIE_EXPR_CAST,
  PIE_EXPR_RANGE,
  PIE_EXPR_MAP,
  PIE_EXPR_MATCH,
  PIE_EXPR_CLOSURE,
  PIE_EXPR_METHOD_CALL,
  PIE_EXPR_MAYBE,
  PIE_EXPR_TERNARY,
  PIE_EXPR_IF,
  PIE_EXPR_POSTFIX,
  PIE_EXPR_TRY,
  PIE_EXPR_THREAD_CALL
} PieExprKind;

typedef enum PieAstTypeKind {
  PIE_AST_TYPE_INFER = 0,
  PIE_AST_TYPE_VOID,
  PIE_AST_TYPE_INT,
  PIE_AST_TYPE_FLOAT,
  PIE_AST_TYPE_CHAR,
  PIE_AST_TYPE_BYTE,
  PIE_AST_TYPE_BOOL,
  PIE_AST_TYPE_STRING,
  PIE_AST_TYPE_REF,
  PIE_AST_TYPE_REF_MUT,
  PIE_AST_TYPE_RAW_PTR,
  PIE_AST_TYPE_STRUCT,
  PIE_AST_TYPE_NULLABLE,
  PIE_AST_TYPE_TUPLE,
  PIE_AST_TYPE_LIST,
  PIE_AST_TYPE_MAP,
  PIE_AST_TYPE_ENUM,
  PIE_AST_TYPE_CLOSURE,
  PIE_AST_TYPE_THREAD,
  PIE_AST_TYPE_MUTEX,
  PIE_AST_TYPE_CHANNEL
} PieAstTypeKind;

#define PIE_ENUM_MAX_VARIANTS 32

typedef struct PieEnumVariant {
  char *name;
  PieAstTypeKind *payload_kinds;
  int *payload_widths;
  size_t payload_count;
} PieEnumVariant;

typedef struct PieEnumDef {
  char *name;
  PieEnumVariant variants[PIE_ENUM_MAX_VARIANTS];
  size_t variant_count;
  int is_pub;
  int is_export;
} PieEnumDef;

#define PIE_AST_TUPLE_MAX_ELEMENTS 8

typedef struct PieAstType {
  PieAstTypeKind kind;
  int width;
  PieAstTypeKind raw_pointee_kind;
  int raw_pointee_width;
  PieAstTypeKind nullable_inner_kind;
  int nullable_inner_width;
  PieAstTypeKind ref_inner_kind;
  int ref_inner_width;
  char *ref_inner_struct_name;
  char *struct_name;
  PieAstTypeKind tuple_element_kinds[PIE_AST_TUPLE_MAX_ELEMENTS];
  int tuple_element_widths[PIE_AST_TUPLE_MAX_ELEMENTS];
  size_t tuple_element_count;
  PieAstTypeKind list_element_kind;
  int list_element_width;
  PieAstTypeKind map_key_kind;
  int map_key_width;
  PieAstTypeKind map_value_kind;
  int map_value_width;
  char *enum_name;
  PieAstTypeKind enum_type_param_kinds[8];
  int enum_type_param_widths[8];
  size_t enum_type_param_count;
  PieAstTypeKind *func_param_kinds;
  int *func_param_widths;
  size_t func_param_count;
  PieAstTypeKind func_return_kind;
  int func_return_width;
} PieAstType;

static inline PieAstType pie_ast_type(PieAstTypeKind kind, int width) {
  PieAstType t;
  t.kind = kind;
  t.width = width;
  t.raw_pointee_kind = PIE_AST_TYPE_INFER;
  t.raw_pointee_width = PIE_WIDTH_INFER;
  t.nullable_inner_kind = PIE_AST_TYPE_INFER;
  t.nullable_inner_width = PIE_WIDTH_INFER;
  t.ref_inner_kind = PIE_AST_TYPE_INFER;
  t.ref_inner_width = PIE_WIDTH_INFER;
  t.struct_name = NULL;
  t.tuple_element_count = 0;
  memset(t.tuple_element_kinds, 0, sizeof(t.tuple_element_kinds));
  memset(t.tuple_element_widths, 0, sizeof(t.tuple_element_widths));
  t.list_element_kind = PIE_AST_TYPE_INFER;
  t.list_element_width = PIE_WIDTH_INFER;
  t.func_param_kinds = NULL;
  t.func_param_widths = NULL;
  t.func_param_count = 0;
  t.func_return_kind = PIE_AST_TYPE_INFER;
  t.func_return_width = PIE_WIDTH_INFER;
  return t;
}

static inline PieAstType pie_ast_type_simple(PieAstTypeKind kind) {
  return pie_ast_type(kind, PIE_WIDTH_INFER);
}

static inline PieAstType pie_ast_type_raw_ptr(PieAstTypeKind pointee_kind,
                                              int pointee_width) {
  PieAstType t = pie_ast_type_simple(PIE_AST_TYPE_RAW_PTR);
  t.raw_pointee_kind = pointee_kind;
  t.raw_pointee_width = pointee_width;
  return t;
}

static inline PieAstType pie_ast_type_nullable(PieAstTypeKind inner_kind,
                                               int inner_width) {
  PieAstType t = pie_ast_type_simple(PIE_AST_TYPE_NULLABLE);
  t.nullable_inner_kind = inner_kind;
  t.nullable_inner_width = inner_width;
  return t;
}

static inline PieAstType pie_ast_type_ref(PieAstTypeKind inner_kind,
                                          int inner_width, int is_mut) {
  PieAstType t =
      pie_ast_type_simple(is_mut ? PIE_AST_TYPE_REF_MUT : PIE_AST_TYPE_REF);
  t.ref_inner_kind = inner_kind;
  t.ref_inner_width = inner_width;
  t.ref_inner_struct_name = NULL;
  return t;
}

typedef struct PieExpr PieExpr;
typedef struct PieProgram PieProgram;

typedef struct PieCallArg {
  PieExpr *expr;
} PieCallArg;

typedef struct PieStructField {
  char *name;
  PieAstType type;
} PieStructField;

typedef struct PieStructDef {
  char *name;
  PieStructField *fields;
  size_t field_count;
  size_t field_capacity;
  int is_pub;
  int is_export;
} PieStructDef;

typedef struct PieConstDef {
  char *name;
  PieAstType type;
  PieExpr *value;
} PieConstDef;

typedef enum PieRequireKind {
  PIE_REQUIRE_STD,
  PIE_REQUIRE_PACKAGE
} PieRequireKind;

typedef struct PieRequire {
  char *path;
  PieRequireKind kind;
} PieRequire;

typedef enum PieThreadOp {
  PIE_THREAD_SPAWN = 0,
  PIE_THREAD_JOIN,
  PIE_THREAD_MUTEX_CREATE,
  PIE_THREAD_MUTEX_LOCK,
  PIE_THREAD_MUTEX_UNLOCK,
  PIE_THREAD_SLEEP,
  PIE_THREAD_CHANNEL_CREATE,
  PIE_THREAD_CHANNEL_SEND,
  PIE_THREAD_CHANNEL_RECV,
  PIE_THREAD_CHANNEL_CLOSE
} PieThreadOp;

struct PieExpr {
  PieExprKind kind;
  long long int_value;
  double float_value;
  unsigned int char_value;
  int bool_value;
  char *string_value;
  size_t string_len;
  char *name;
  char *call_name;
  char op;
  char op_text[8];
  PieExpr *left;
  PieExpr *right;
  PieCallArg *call_args;
  size_t call_arg_count;
  size_t call_arg_capacity;
  char *new_type_name;
  char *field_name;
  PieExpr **tuple_elements;
  size_t tuple_element_count;
  PieExpr **list_elements;
  size_t list_element_count;
  PieExpr *index_object;
  PieExpr *index_expr;
  char *enum_name;
  char *variant_name;
  int variant_tag;
  PieExpr *cast_inner;
  PieAstTypeKind cast_target_kind;
  int cast_target_width;
  PieExpr *ternary_false;
  PieExpr *range_start;
  PieExpr *range_end;
  int range_inclusive;
  PieExpr **map_keys;
  PieExpr **map_values;
  size_t map_entry_count;
  PieExpr *match_expr_target;
  char **match_expr_case_names;
  PieProgram **match_expr_case_bodies;
  size_t match_expr_case_count;
  PieProgram *match_expr_default;
  char ***match_expr_case_bindings;
  size_t *match_expr_case_binding_counts;
  int *match_expr_case_tags;
  char **closure_param_names;
  PieAstType *closure_param_types;
  size_t closure_param_count;
  PieAstType closure_return_type;
  PieProgram *closure_body;
  char *method_name;
  char **closure_capture_names;
  PieAstTypeKind *closure_capture_types;
  size_t closure_capture_count;
  PieExpr *if_condition;
  PieExpr *if_then;
  PieExpr *if_else;
  char **interp_texts;
  size_t *interp_text_lens;
  PieExpr **interp_exprs;
  size_t interp_part_count;
  int thread_op;
};

typedef struct PiePrintArg {
  int is_string;
  char *text;
  size_t text_len;
  PieExpr *expr;
} PiePrintArg;

typedef enum PieStmtKind {
  PIE_STMT_PRINT,
  PIE_STMT_EXPR,
  PIE_STMT_LET,
  PIE_STMT_ASSIGN,
  PIE_STMT_RETURN,
  PIE_STMT_IF,
  PIE_STMT_WHILE,
  PIE_STMT_REGION,
  PIE_STMT_UNSAFE,
  PIE_STMT_BREAK,
  PIE_STMT_CONTINUE,
  PIE_STMT_FOR,
  PIE_STMT_ASSIGN_MULTI,
  PIE_STMT_RAW_STORE,
  PIE_STMT_STRUCT,
  PIE_STMT_FIELD_ASSIGN,
  PIE_STMT_INDEX_ASSIGN,
  PIE_STMT_BLOCK,
  PIE_STMT_ENUM,
  PIE_STMT_MATCH,
  PIE_STMT_PASS,
  PIE_STMT_DEFER,
  PIE_STMT_CONST,
  PIE_STMT_TYPE_ALIAS,
  PIE_STMT_ASSERT,
  PIE_STMT_ASSERT_EQ,
  PIE_STMT_DO_WHILE
} PieStmtKind;

typedef struct PieStmt {
  PieStmtKind kind;
  int println;
  int is_mut;
  PieAstType type_annotation;
  char *name;
  char assign_op[4];
  PieExpr *target;
  PieExpr *expr;
  PiePrintArg *args;
  size_t arg_count;
  PieProgram *then_branch;
  PieProgram *else_branch;
  char *for_var_name;
  char *label_name;
  PieExpr *for_start;
  PieExpr *for_end;
  int for_inclusive;
  PieStructDef *struct_def;
  PieExpr *field_target;
  PieExpr *index_target;
  PieExpr *index_expr;
  char **multi_names;
  PieExpr **multi_exprs;
  size_t multi_count;
  PieEnumDef *enum_def;
  PieExpr *match_target;
  char **match_case_names;
  PieProgram **match_case_bodies;
  size_t match_case_count;
  PieProgram *match_default;
  char ***match_case_bindings;
  size_t *match_case_binding_counts;
  PieConstDef *const_def;
} PieStmt;

typedef struct PieFunction {
  char *name;
  PieAstType return_type;
  int is_unsafe;
  int is_pub;
  int is_export;
  char **param_names;
  PieAstType *param_types;
  size_t param_count;
  char **type_params;
  size_t type_param_count;
  char **type_param_constraints;
  PieProgram *body;
  int borrowed_body;
} PieFunction;

struct PieProgram {
  PieAstType main_return_type;
  int has_main;
  PieStmt *stmts;
  size_t stmt_count;
  size_t stmt_capacity;
  PieFunction *functions;
  size_t function_count;
  size_t function_capacity;
  PieStructDef *structs;
  size_t struct_count;
  size_t struct_capacity;
  PieRequire *
    requires;
  size_t require_count;
  size_t require_capacity;
  PieEnumDef *enums;
  size_t enum_count;
  size_t enum_capacity;
  PieConstDef *consts;
  size_t const_count;
  size_t const_capacity;
  struct PieProgram **owned_modules;
  size_t owned_module_count;
  size_t owned_module_capacity;
};

void pie_program_init(PieProgram *program);
void pie_program_free(PieProgram *program);
int pie_program_push_stmt(PieProgram *program, PieStmt stmt);
int pie_program_push_function(PieProgram *program, PieFunction function);
int pie_program_push_owned_module(PieProgram *program, PieProgram *module);

PieExpr *pie_expr_int(long long value);
PieExpr *pie_expr_float(double value);
PieExpr *pie_expr_char(unsigned int value);
PieExpr *pie_expr_bool(int value);
PieExpr *pie_expr_maybe(void);
PieExpr *pie_expr_null(void);
PieExpr *pie_expr_string(const char *value, size_t len);
PieExpr *pie_expr_var(const char *name);
PieExpr *pie_expr_call(const char *name);
int pie_expr_call_add_arg(PieExpr *call, PieExpr *arg);
PieExpr *pie_expr_binary(char op, PieExpr *left, PieExpr *right);
PieExpr *pie_expr_binary_op(const char *op, PieExpr *left, PieExpr *right);
PieExpr *pie_expr_unary(char op, PieExpr *inner);
PieExpr *pie_expr_unary_op(const char *op, PieExpr *inner);
PieExpr *pie_expr_postfix(const char *op, PieExpr *inner);
PieExpr *pie_expr_try(PieExpr *inner);
void pie_expr_free(PieExpr *expr);

PieExpr *pie_expr_new(const char *type_name);
int pie_expr_new_add_arg(PieExpr *new_expr, const char *field_name,
                         PieExpr *value);
PieExpr *pie_expr_field(PieExpr *object, const char *field_name);

PieExpr *pie_expr_tuple(size_t element_count);
int pie_expr_tuple_add_element(PieExpr *tuple, PieExpr *element);

PieExpr *pie_expr_list(size_t element_count);
int pie_expr_list_add_element(PieExpr *list, PieExpr *element);

static inline PieAstType pie_ast_type_list(PieAstTypeKind element_kind,
                                           int element_width) {
  PieAstType t = pie_ast_type_simple(PIE_AST_TYPE_LIST);
  t.list_element_kind = element_kind;
  t.list_element_width = element_width;
  return t;
}

static inline PieAstType pie_ast_type_map(PieAstTypeKind key_kind,
                                          int key_width,
                                          PieAstTypeKind value_kind,
                                          int value_width) {
  PieAstType t = pie_ast_type_simple(PIE_AST_TYPE_MAP);
  t.map_key_kind = key_kind;
  t.map_key_width = key_width;
  t.map_value_kind = value_kind;
  t.map_value_width = value_width;
  return t;
}

PieExpr *pie_expr_index(PieExpr *object, PieExpr *index);

PieExpr *pie_expr_variant(const char *enum_name, const char *variant_name);
PieExpr *pie_expr_cast(PieExpr *inner, PieAstTypeKind target_kind,
                       int target_width);
PieExpr *pie_expr_range(PieExpr *start, PieExpr *end, int inclusive);
PieExpr *pie_expr_ternary(PieExpr *cond, PieExpr *true_expr,
                          PieExpr *false_expr);
PieExpr *pie_expr_map(void);
int pie_expr_map_add(PieExpr *map, PieExpr *key, PieExpr *value);
PieExpr *pie_expr_match(PieExpr *target);
PieExpr *pie_expr_closure(void);
PieExpr *pie_expr_method_call(PieExpr *object, const char *method_name);
int pie_expr_method_call_add_arg(PieExpr *call, PieExpr *arg);
PieExpr *pie_expr_thread_call(PieThreadOp op);
int pie_expr_thread_call_add_arg(PieExpr *call, PieExpr *arg);

int pie_program_push_enum(PieProgram *program, PieEnumDef def);
const PieEnumDef *pie_program_find_enum(const PieProgram *program,
                                        const char *name);

int pie_program_push_struct(PieProgram *program, PieStructDef def);

int pie_program_push_const(PieProgram *program, PieConstDef def);
const PieConstDef *pie_program_find_const(const PieProgram *program,
                                          const char *name);
const PieStructDef *pie_program_find_struct(const PieProgram *program,
                                            const char *name);
int pie_program_push_require(PieProgram *program, const char *path,
                             PieRequireKind kind);

#endif
