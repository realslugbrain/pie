#ifndef PIE_CORE_IR_IR_H
#define PIE_CORE_IR_IR_H

#include <stddef.h>
#include <stdio.h>

#include "pie/core/type_width.h"

typedef struct PieStructDef PieStructDef;

typedef enum PieIrExprKind {
  PIE_IR_EXPR_INT,
  PIE_IR_EXPR_FLOAT,
  PIE_IR_EXPR_CHAR,
  PIE_IR_EXPR_BOOL,
  PIE_IR_EXPR_STRING,
  PIE_IR_EXPR_LOCAL,
  PIE_IR_EXPR_CALL,
  PIE_IR_EXPR_BINARY,
  PIE_IR_EXPR_UNARY,
  PIE_IR_EXPR_NEW,
  PIE_IR_EXPR_FIELD,
  PIE_IR_EXPR_NULL,
  PIE_IR_EXPR_TUPLE,
  PIE_IR_EXPR_LIST,
  PIE_IR_EXPR_INDEX,
  PIE_IR_EXPR_VARIANT,
  PIE_IR_EXPR_CAST,
  PIE_IR_EXPR_RANGE,
  PIE_IR_EXPR_MAP,
  PIE_IR_EXPR_MATCH,
  PIE_IR_EXPR_CLOSURE,
  PIE_IR_EXPR_METHOD_CALL,
  PIE_IR_EXPR_CLOSURE_CALL,
  PIE_IR_EXPR_MAYBE,
  PIE_IR_EXPR_TERNARY,
  PIE_IR_EXPR_IF,
  PIE_IR_EXPR_POSTFIX,
  PIE_IR_EXPR_THREAD_SPAWN,
  PIE_IR_EXPR_THREAD_JOIN,
  PIE_IR_EXPR_MUTEX_CREATE,
  PIE_IR_EXPR_MUTEX_LOCK,
  PIE_IR_EXPR_MUTEX_UNLOCK,
  PIE_IR_EXPR_THREAD_SLEEP,
  PIE_IR_EXPR_CHANNEL_CREATE,
  PIE_IR_EXPR_CHANNEL_SEND,
  PIE_IR_EXPR_CHANNEL_RECV,
  PIE_IR_EXPR_CHANNEL_CLOSE,
  PIE_IR_EXPR_FORMAT
} PieIrExprKind;

typedef enum PieIrTypeKind {
  PIE_IR_TYPE_UNKNOWN = 0,
  PIE_IR_TYPE_VOID,
  PIE_IR_TYPE_INT,
  PIE_IR_TYPE_FLOAT,
  PIE_IR_TYPE_CHAR,
  PIE_IR_TYPE_BYTE,
  PIE_IR_TYPE_BOOL,
  PIE_IR_TYPE_STRING,
  PIE_IR_TYPE_REF,
  PIE_IR_TYPE_REF_MUT,
  PIE_IR_TYPE_RAW_PTR,
  PIE_IR_TYPE_STRUCT,
  PIE_IR_TYPE_NULL,
  PIE_IR_TYPE_NULLABLE,
  PIE_IR_TYPE_TUPLE,
  PIE_IR_TYPE_LIST,
  PIE_IR_TYPE_MAP,
  PIE_IR_TYPE_ENUM,
  PIE_IR_TYPE_CLOSURE,
  PIE_IR_TYPE_THREAD,
  PIE_IR_TYPE_MUTEX,
  PIE_IR_TYPE_CHANNEL
} PieIrTypeKind;

typedef struct PieIrExpr PieIrExpr;
typedef struct PieIrProgram PieIrProgram;

typedef struct PieIrCallArg {
  PieIrExpr *expr;
} PieIrCallArg;

struct PieIrExpr {
  PieIrExprKind kind;
  PieIrTypeKind type;
  int type_width;
  PieIrTypeKind raw_pointee_type;
  int raw_pointee_width;
  PieIrTypeKind nullable_inner_type;
  int nullable_inner_width;
  PieIrTypeKind ref_inner_type;
  int ref_inner_width;
  long long int_value;
  double float_value;
  unsigned int char_value;
  int bool_value;
  char *string_value;
  size_t string_len;
  size_t local_id;
  char *call_name;
  char op;
  char op_text[8];
  PieIrExpr *left;
  PieIrExpr *right;
  PieIrCallArg *call_args;
  size_t call_arg_count;
  size_t call_arg_capacity;
  char *struct_name;
  char *field_name;
  int field_offset;
  PieIrExpr **tuple_elements;
  size_t tuple_element_count;
  PieIrTypeKind tuple_element_types[8];
  int tuple_element_widths[8];
  PieIrExpr **list_elements;
  size_t list_element_count;
  PieIrTypeKind list_element_type;
  int list_element_width;
  char *enum_name;
  char *variant_name;
  int variant_tag;
  PieIrExpr *cast_inner;
  PieIrTypeKind cast_target_type;
  int cast_target_width;
  PieIrExpr *ternary_false;
  PieIrExpr *range_start;
  PieIrExpr *range_end;
  int range_inclusive;
  PieIrExpr **map_keys;
  PieIrExpr **map_values;
  size_t map_entry_count;
  PieIrExpr *match_expr_target;
  char **match_expr_case_names;
  PieIrProgram **match_expr_case_bodies;
  size_t match_expr_case_count;
  PieIrProgram *match_expr_default;
  char ***match_expr_case_bindings;
  size_t **match_expr_case_binding_ids;
  size_t *match_expr_case_binding_counts;
  int *match_expr_case_tags;
  PieIrExpr **match_expr_value_exprs;
  PieIrExpr *match_expr_default_value;
  char **closure_param_names;
  PieIrTypeKind *closure_param_types;
  size_t closure_param_count;
  PieIrTypeKind closure_return_type;
  PieIrProgram *closure_body;
  char **closure_captured_names;
  size_t closure_captured_count;
  char *method_name;
  PieIrTypeKind *closure_capture_types;
  size_t *closure_capture_outer_ids;
  PieIrExpr *if_condition;
  PieIrExpr *if_then;
  PieIrExpr *if_else;
  PieIrExpr *format_template;
  PieIrExpr **format_args;
  size_t format_arg_count;
};

typedef struct PieIrPrintArg {
  int is_string;
  char *text;
  size_t text_len;
  PieIrExpr *expr;
} PieIrPrintArg;

typedef enum PieIrStmtKind {
  PIE_IR_STMT_EXPR,
  PIE_IR_STMT_LET,
  PIE_IR_STMT_ASSIGN,
  PIE_IR_STMT_PRINT,
  PIE_IR_STMT_RETURN,
  PIE_IR_STMT_IF,
  PIE_IR_STMT_WHILE,
  PIE_IR_STMT_REGION,
  PIE_IR_STMT_UNSAFE,
  PIE_IR_STMT_BREAK,
  PIE_IR_STMT_CONTINUE,
  PIE_IR_STMT_RAW_STORE,
  PIE_IR_STMT_STRUCT,
  PIE_IR_STMT_FIELD_ASSIGN,
  PIE_IR_STMT_INDEX_ASSIGN,
  PIE_IR_STMT_MATCH,
  PIE_IR_STMT_PASS,
  PIE_IR_STMT_DEFER,
  PIE_IR_STMT_ASSERT,
  PIE_IR_STMT_ASSERT_EQ,
  PIE_IR_STMT_DO_WHILE
} PieIrStmtKind;

typedef struct PieIrStmt {
  PieIrStmtKind kind;
  size_t local_id;
  int is_mut;
  char assign_op[4];
  int println;
  PieIrExpr *target;
  PieIrExpr *expr;
  PieIrPrintArg *args;
  size_t arg_count;
  PieIrProgram *then_branch;
  PieIrProgram *else_branch;
  char *struct_name;
  char *field_name;
  PieIrExpr *field_target;
  PieIrExpr *index_target;
  PieIrExpr *index_expr;
  PieIrExpr *match_target;
  char **match_case_names;
  PieIrProgram **match_case_bodies;
  size_t match_case_count;
  PieIrProgram *match_default;
  char ***match_case_bindings;
  size_t **match_case_binding_ids;
  size_t *match_case_binding_counts;
  int *match_case_tags;
  PieIrExpr *assert_cond;
  PieIrExpr *assert_left;
  PieIrExpr *assert_right;
  char *label_name;
} PieIrStmt;

typedef struct PieIrLocal {
  char *name;
  int is_mut;
  PieIrTypeKind type;
  int type_width;
  PieIrTypeKind raw_pointee_type;
  int raw_pointee_width;
  PieIrTypeKind nullable_inner_type;
  int nullable_inner_width;
  PieIrTypeKind ref_inner_type;
  int ref_inner_width;
  PieIrTypeKind list_element_type;
  int list_element_width;
  char *struct_name;
  char *enum_name;
} PieIrLocal;

typedef struct PieIrFunction {
  char *name;
  PieIrTypeKind return_type;
  int return_type_width;
  PieIrTypeKind return_raw_pointee_type;
  int return_raw_pointee_width;
  PieIrTypeKind return_ref_inner_type;
  int return_ref_inner_width;
  char **param_names;
  PieIrTypeKind *param_types;
  int *param_type_widths;
  PieIrTypeKind *param_raw_pointee_types;
  int *param_raw_pointee_widths;
  PieIrTypeKind *param_ref_inner_types;
  int *param_ref_inner_widths;
  PieIrTypeKind *param_nullable_inner_types;
  int *param_nullable_inner_widths;
  size_t *param_local_ids;
  size_t param_count;
  PieIrProgram *body;
} PieIrFunction;

struct PieIrProgram {
  PieIrLocal *locals;
  size_t local_count;
  size_t local_capacity;
  PieIrStmt *stmts;
  size_t stmt_count;
  size_t stmt_capacity;
  PieIrFunction *functions;
  size_t function_count;
  size_t function_capacity;
  PieStructDef *structs;
  size_t struct_count;
  size_t struct_capacity;
};

void pie_ir_program_init(PieIrProgram *program);
void pie_ir_program_free(PieIrProgram *program);
int pie_ir_program_add_local(PieIrProgram *program, const char *name,
                             int is_mut, size_t *out_id);
int pie_ir_program_add_typed_local(PieIrProgram *program, const char *name,
                                   int is_mut, PieIrTypeKind type,
                                   int type_width, size_t *out_id);
int pie_ir_program_add_raw_typed_local(PieIrProgram *program, const char *name,
                                       int is_mut, PieIrTypeKind type,
                                       int type_width,
                                       PieIrTypeKind raw_pointee_type,
                                       int raw_pointee_width, size_t *out_id);
int pie_ir_program_add_nullable_local(PieIrProgram *program, const char *name,
                                      int is_mut, PieIrTypeKind inner_type,
                                      int inner_width, size_t *out_id);
int pie_ir_program_find_local(const PieIrProgram *program, const char *name,
                              size_t *out_id, int *out_is_mut,
                              PieIrTypeKind *out_type, int *out_type_width,
                              PieIrTypeKind *out_raw_pointee_type,
                              int *out_raw_pointee_width);
int pie_ir_program_push_stmt(PieIrProgram *program, PieIrStmt stmt);
int pie_ir_program_push_function(PieIrProgram *program, PieIrFunction function);
void pie_ir_program_write_text(const PieIrProgram *program, FILE *out);
const char *pie_ir_type_name(PieIrTypeKind type);

PieIrExpr *pie_ir_expr_int(long long value);
PieIrExpr *pie_ir_expr_float(double value);
PieIrExpr *pie_ir_expr_char(unsigned int value);
PieIrExpr *pie_ir_expr_bool(int value);
PieIrExpr *pie_ir_expr_maybe(void);
PieIrExpr *pie_ir_expr_format(PieIrExpr *template, PieIrExpr **args,
                              size_t arg_count);
PieIrExpr *pie_ir_expr_null(void);
PieIrExpr *pie_ir_expr_string(const char *value, size_t len);
PieIrExpr *pie_ir_expr_local(size_t local_id, PieIrTypeKind type,
                             int type_width, PieIrTypeKind ref_inner_type,
                             int ref_inner_width);
PieIrExpr *pie_ir_expr_raw_local(size_t local_id, PieIrTypeKind type,
                                 int type_width, PieIrTypeKind raw_pointee_type,
                                 int raw_pointee_width);
PieIrExpr *pie_ir_expr_call(const char *name, PieIrTypeKind return_type);
int pie_ir_expr_call_add_arg(PieIrExpr *call, PieIrExpr *arg);
PieIrExpr *pie_ir_expr_binary(char op, PieIrExpr *left, PieIrExpr *right);
PieIrExpr *pie_ir_expr_binary_typed(const char *op, PieIrExpr *left,
                                    PieIrExpr *right, PieIrTypeKind type);
PieIrExpr *pie_ir_expr_unary(char op, PieIrExpr *inner);
PieIrExpr *pie_ir_expr_unary_typed(const char *op, PieIrExpr *inner,
                                   PieIrTypeKind type);
void pie_ir_expr_free(PieIrExpr *expr);

PieIrExpr *pie_ir_expr_new(const char *struct_name);
PieIrExpr *pie_ir_expr_field(PieIrExpr *object, const char *field_name);
PieIrExpr *pie_ir_expr_tuple(size_t element_count);
int pie_ir_expr_tuple_add_element(PieIrExpr *tuple, PieIrExpr *element);

PieIrExpr *pie_ir_expr_list(size_t element_count);
int pie_ir_expr_list_add_element(PieIrExpr *list, PieIrExpr *element);

PieIrExpr *pie_ir_expr_index(PieIrExpr *object, PieIrExpr *index);

PieIrExpr *pie_ir_expr_variant(const char *enum_name, const char *variant_name);
PieIrExpr *pie_ir_expr_cast(PieIrExpr *inner, PieIrTypeKind target_type,
                            int target_width);
PieIrExpr *pie_ir_expr_ternary(PieIrExpr *cond, PieIrExpr *true_expr,
                               PieIrExpr *false_expr);
PieIrExpr *pie_ir_expr_if(PieIrExpr *cond, PieIrExpr *then_expr,
                          PieIrExpr *else_expr);
PieIrExpr *pie_ir_expr_range(PieIrExpr *start, PieIrExpr *end, int inclusive);
PieIrExpr *pie_ir_expr_map(void);
int pie_ir_expr_map_add(PieIrExpr *map, PieIrExpr *key, PieIrExpr *value);
PieIrExpr *pie_ir_expr_match(PieIrExpr *target);
PieIrExpr *pie_ir_expr_closure(void);
PieIrExpr *pie_ir_expr_method_call(PieIrExpr *object, const char *method_name,
                                   PieIrTypeKind return_type);
PieIrExpr *pie_ir_expr_closure_call(PieIrExpr *closure,
                                    PieIrTypeKind return_type);

int pie_ir_program_add_struct(PieIrProgram *program, PieStructDef def);
const PieStructDef *pie_ir_program_find_struct(const PieIrProgram *program,
                                               const char *name);
int pie_ir_program_add_struct_local(PieIrProgram *program, const char *name,
                                    int is_mut, const char *struct_name,
                                    size_t *out_id);

#endif
