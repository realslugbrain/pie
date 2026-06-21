#define _POSIX_C_SOURCE 200809L
#include "pie/backend/asm/asm_emit.h"

#include "pie/backend/asm/asm_codegen.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct LocalSlot {
  size_t local_id;
  int offset;
  int is_mut;
  PieIrTypeKind type;
  int type_width;
  PieIrTypeKind ref_inner_type;
  int ref_inner_width;
} LocalSlot;

typedef struct StringLiteral {
  char *bytes;
  size_t len;
  size_t id;
} StringLiteral;

typedef struct LoopLabels {
  int continue_label;
  int break_label;
  char *label_name;
} LoopLabels;

typedef struct PieAsmFrame {
  LocalSlot *locals;
  size_t local_count;
  size_t local_capacity;
  LoopLabels *loop_labels;
  size_t loop_label_count;
  size_t loop_label_capacity;
  int next_stack_offset;
  int did_return;
  PieIrExpr **deferred_exprs;
  size_t deferred_count;
  size_t deferred_capacity;
  struct PieAsmFrame *prev;
} PieAsmFrame;

struct PieAsmCodegen {
  FILE *out;
  PieDiagnosticBag *diag;
  LocalSlot *locals;
  size_t local_count;
  size_t local_capacity;
  StringLiteral *strings;
  size_t string_count;
  size_t string_capacity;
  LoopLabels *loop_labels;
  size_t loop_label_count;
  size_t loop_label_capacity;
  int next_stack_offset;
  int label_id;
  int did_return;
  int in_closure_body;
  char **closure_capture_names;
  size_t closure_capture_count;
  PieAsmFrame *frame_stack;
  PieIrExpr **deferred_exprs;
  size_t deferred_count;
  size_t deferred_capacity;
};

static void codegen_free(PieAsmCodegen *cg) {
  for (size_t i = 0; i < cg->string_count; i++) {
    free(cg->strings[i].bytes);
  }
  free(cg->locals);
  free(cg->strings);
  free(cg->loop_labels);
  for (size_t i = 0; i < cg->deferred_count; i++) {
    pie_ir_expr_free(cg->deferred_exprs[i]);
  }
  free(cg->deferred_exprs);

  while (cg->frame_stack) {
    PieAsmFrame *frame = cg->frame_stack;
    cg->frame_stack = frame->prev;
    free(frame->locals);
    free(frame->loop_labels);
    free(frame);
  }
}

static void api_push_frame(PieAsmCodegen *cg) {
  PieAsmFrame *frame = (PieAsmFrame *)malloc(sizeof(PieAsmFrame));
  if (!frame)
    return;
  frame->locals = cg->locals;
  frame->local_count = cg->local_count;
  frame->local_capacity = cg->local_capacity;
  frame->loop_labels = cg->loop_labels;
  frame->loop_label_count = cg->loop_label_count;
  frame->loop_label_capacity = cg->loop_label_capacity;
  frame->next_stack_offset = cg->next_stack_offset;
  frame->did_return = cg->did_return;
  frame->prev = cg->frame_stack;
  cg->frame_stack = frame;

  cg->locals = NULL;
  cg->local_count = 0;
  cg->local_capacity = 0;
  cg->loop_labels = NULL;
  cg->loop_label_count = 0;
  cg->loop_label_capacity = 0;
  cg->next_stack_offset = 0;
  cg->did_return = 0;
}

static void api_pop_frame(PieAsmCodegen *cg) {
  if (!cg->frame_stack)
    return;

  free(cg->locals);
  free(cg->loop_labels);

  PieAsmFrame *frame = cg->frame_stack;
  cg->locals = frame->locals;
  cg->local_count = frame->local_count;
  cg->local_capacity = frame->local_capacity;
  cg->loop_labels = frame->loop_labels;
  cg->loop_label_count = frame->loop_label_count;
  cg->loop_label_capacity = frame->loop_label_capacity;
  cg->next_stack_offset = frame->next_stack_offset;
  cg->did_return = frame->did_return;
  cg->frame_stack = frame->prev;
  free(frame);
}

static void api_set_stack_offset(PieAsmCodegen *cg, int offset) {
  cg->next_stack_offset = offset;
}

static LocalSlot *find_local_internal(PieAsmCodegen *cg, size_t local_id) {
  for (size_t i = 0; i < cg->local_count; i++) {
    if (cg->locals[i].local_id == local_id) {
      return &cg->locals[i];
    }
  }
  return NULL;
}

static void api_emit(PieAsmCodegen *cg, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(cg->out, fmt, args);
  va_end(args);
}

static int api_add_local(PieAsmCodegen *cg, size_t local_id, int is_mut,
                         PieIrTypeKind type, int type_width,
                         PieIrTypeKind ref_inner_type, int ref_inner_width,
                         PieAsmSymbol *out_symbol) {
  LocalSlot *existing = find_local_internal(cg, local_id);
  if (existing) {
    out_symbol->offset = existing->offset;
    out_symbol->is_mut = existing->is_mut;
    out_symbol->type = existing->type;
    out_symbol->type_width = existing->type_width;
    out_symbol->ref_inner_type = existing->ref_inner_type;
    out_symbol->ref_inner_width = existing->ref_inner_width;
    return 1;
  }

  if (cg->local_count == cg->local_capacity) {
    size_t next_capacity = cg->local_capacity ? cg->local_capacity * 2 : 16;
    LocalSlot *next =
        (LocalSlot *)realloc(cg->locals, next_capacity * sizeof(LocalSlot));
    if (!next) {
      pie_diag_error(cg->diag, "out of memory while storing local slot");
      return 0;
    }
    cg->locals = next;
    cg->local_capacity = next_capacity;
  }

  LocalSlot *symbol = &cg->locals[cg->local_count++];
  symbol->local_id = local_id;
  int is_fat_ref =
      (type == PIE_IR_TYPE_REF && ref_inner_type == PIE_IR_TYPE_STRING);
  int alloc_size = (type == PIE_IR_TYPE_STRING || is_fat_ref) ? 16
                   : (type == PIE_IR_TYPE_CLOSURE)            ? 16
                                                              : 8;
  cg->next_stack_offset += alloc_size;
  symbol->offset = cg->next_stack_offset;
  symbol->is_mut = is_mut;
  symbol->type = type;
  symbol->type_width = type_width;
  symbol->ref_inner_type = ref_inner_type;
  symbol->ref_inner_width = ref_inner_width;
  out_symbol->offset = symbol->offset;
  out_symbol->is_mut = symbol->is_mut;
  out_symbol->type = symbol->type;
  out_symbol->type_width = symbol->type_width;
  out_symbol->ref_inner_type = symbol->ref_inner_type;
  out_symbol->ref_inner_width = symbol->ref_inner_width;
  return 1;
}

static int api_find_local(PieAsmCodegen *cg, size_t local_id,
                          PieAsmSymbol *out_symbol) {
  LocalSlot *symbol = find_local_internal(cg, local_id);
  if (!symbol) {
    return 0;
  }
  out_symbol->offset = symbol->offset;
  out_symbol->is_mut = symbol->is_mut;
  out_symbol->type = symbol->type;
  out_symbol->type_width = symbol->type_width;
  out_symbol->ref_inner_type = symbol->ref_inner_type;
  out_symbol->ref_inner_width = symbol->ref_inner_width;
  return 1;
}

static int api_add_local_at(PieAsmCodegen *cg, size_t local_id, int offset,
                            int is_mut, PieIrTypeKind type, int type_width,
                            PieAsmSymbol *out_symbol) {
  LocalSlot *existing = find_local_internal(cg, local_id);
  if (existing) {
    out_symbol->offset = existing->offset;
    out_symbol->is_mut = existing->is_mut;
    out_symbol->type = existing->type;
    out_symbol->type_width = existing->type_width;
    return 1;
  }

  if (cg->local_count == cg->local_capacity) {
    size_t next_capacity = cg->local_capacity ? cg->local_capacity * 2 : 16;
    LocalSlot *next =
        (LocalSlot *)realloc(cg->locals, next_capacity * sizeof(LocalSlot));
    if (!next) {
      pie_diag_error(cg->diag, "out of memory while storing local slot");
      return 0;
    }
    cg->locals = next;
    cg->local_capacity = next_capacity;
  }

  LocalSlot *symbol = &cg->locals[cg->local_count++];
  symbol->local_id = local_id;
  symbol->offset = offset;
  symbol->is_mut = is_mut;
  symbol->type = type;
  symbol->type_width = type_width;
  out_symbol->offset = symbol->offset;
  out_symbol->is_mut = symbol->is_mut;
  out_symbol->type = symbol->type;
  out_symbol->type_width = symbol->type_width;
  return 1;
}

static int api_add_string(PieAsmCodegen *cg, const char *bytes, size_t len,
                          size_t *out_id) {
  if (cg->string_count == cg->string_capacity) {
    size_t next_capacity = cg->string_capacity ? cg->string_capacity * 2 : 16;
    StringLiteral *next = (StringLiteral *)realloc(
        cg->strings, next_capacity * sizeof(StringLiteral));
    if (!next) {
      pie_diag_error(cg->diag, "out of memory while storing string literal");
      return 0;
    }
    cg->strings = next;
    cg->string_capacity = next_capacity;
  }

  size_t id = cg->string_count;
  StringLiteral *lit = &cg->strings[cg->string_count++];
  lit->bytes = (char *)malloc(len ? len : 1);
  if (!lit->bytes) {
    pie_diag_error(cg->diag, "out of memory while copying string literal");
    return 0;
  }
  if (len) {
    memcpy(lit->bytes, bytes, len);
  }
  lit->len = len;
  lit->id = id;
  *out_id = id;
  return 1;
}

static void api_error(PieAsmCodegen *cg, const char *message) {
  pie_diag_error(cg->diag, message);
}

static void api_errorf(PieAsmCodegen *cg, const char *fmt, ...) {
  char stack_buf[1024];
  va_list args;
  va_start(args, fmt);
  int needed = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
  va_end(args);

  if (needed < 0) {
    pie_diag_error(cg->diag, "internal codegen diagnostic formatting error");
    return;
  }
  if ((size_t)needed < sizeof(stack_buf)) {
    pie_diag_error(cg->diag, stack_buf);
    return;
  }

  char *heap_buf = (char *)malloc((size_t)needed + 1);
  if (!heap_buf) {
    pie_diag_error(cg->diag,
                   "out of memory while formatting codegen diagnostic");
    return;
  }
  va_start(args, fmt);
  vsnprintf(heap_buf, (size_t)needed + 1, fmt, args);
  va_end(args);
  pie_diag_error(cg->diag, heap_buf);
  free(heap_buf);
}

static PieDiagnosticBag *api_diag(PieAsmCodegen *cg) { return cg->diag; }

static void api_mark_returned(PieAsmCodegen *cg) { cg->did_return = 1; }

static int emit_expr(PieAsmCodegen *cg, const PieIrExpr *expr);

static int api_emit_deferred(PieAsmCodegen *cg) {
  for (size_t i = 0; i < cg->deferred_count; i++) {
    if (!emit_expr(cg, cg->deferred_exprs[i])) {
      return 0;
    }
  }
  return 1;
}

static int api_next_label(PieAsmCodegen *cg) { return cg->label_id++; }

static int api_push_loop(PieAsmCodegen *cg, int continue_label, int break_label,
                         const char *label_name) {
  if (cg->loop_label_count == cg->loop_label_capacity) {
    size_t next_capacity =
        cg->loop_label_capacity ? cg->loop_label_capacity * 2 : 8;
    LoopLabels *next = (LoopLabels *)realloc(
        cg->loop_labels, next_capacity * sizeof(LoopLabels));
    if (!next) {
      pie_diag_error(cg->diag, "out of memory while storing loop labels");
      return 0;
    }
    cg->loop_labels = next;
    cg->loop_label_capacity = next_capacity;
  }
  cg->loop_labels[cg->loop_label_count].continue_label = continue_label;
  cg->loop_labels[cg->loop_label_count].break_label = break_label;
  cg->loop_labels[cg->loop_label_count].label_name =
      label_name ? strdup(label_name) : NULL;
  cg->loop_label_count++;
  return 1;
}

static void api_pop_loop(PieAsmCodegen *cg) {
  if (cg->loop_label_count > 0) {
    cg->loop_label_count--;
    free(cg->loop_labels[cg->loop_label_count].label_name);
    cg->loop_labels[cg->loop_label_count].label_name = NULL;
  }
}

static int api_current_loop(PieAsmCodegen *cg, int *out_continue_label,
                            int *out_break_label) {
  if (cg->loop_label_count == 0) {
    return 0;
  }
  const LoopLabels *labels = &cg->loop_labels[cg->loop_label_count - 1];
  *out_continue_label = labels->continue_label;
  *out_break_label = labels->break_label;
  return 1;
}

static int api_find_loop_label(PieAsmCodegen *cg, const char *label_name,
                               int *out_continue_label, int *out_break_label) {
  for (size_t i = cg->loop_label_count; i > 0; i--) {
    const LoopLabels *labels = &cg->loop_labels[i - 1];
    if (labels->label_name && strcmp(labels->label_name, label_name) == 0) {
      *out_continue_label = labels->continue_label;
      *out_break_label = labels->break_label;
      return 1;
    }
  }
  return 0;
}

static int api_find_capture(PieAsmCodegen *cg, const char *name,
                            int *out_offset) {
  if (!cg->in_closure_body || !name)
    return 0;
  for (size_t i = 0; i < cg->closure_capture_count; i++) {
    if (cg->closure_capture_names[i] &&
        strcmp(cg->closure_capture_names[i], name) == 0) {
      *out_offset = (int)(i * 8);
      return 1;
    }
  }
  return 0;
}

static void api_set_closure_context(PieAsmCodegen *cg, int in_closure,
                                    char **names, size_t count) {
  cg->in_closure_body = in_closure;
  cg->closure_capture_names = names;
  cg->closure_capture_count = count;
}

static int api_in_closure_body(PieAsmCodegen *cg) {
  return cg->in_closure_body;
}

static size_t api_closure_capture_count(PieAsmCodegen *cg) {
  return cg->closure_capture_count;
}

static const char *api_closure_capture_name(PieAsmCodegen *cg, size_t index) {
  if (index >= cg->closure_capture_count)
    return NULL;
  return cg->closure_capture_names ? cg->closure_capture_names[index] : NULL;
}

static const PieAsmCodegenApi *asm_api(void);

static int emit_expr(PieAsmCodegen *cg, const PieIrExpr *expr) {
  const PieAsmHookRegistry *registry = pie_asm_hook_registry();
  PieAsmCodegenContext ctx;
  ctx.cg = cg;
  ctx.api = asm_api();

  for (size_t i = 0; i < registry->expr_hook_count; i++) {
    PieAsmGenResult result = registry->expr_hooks[i].generate(&ctx, expr);
    if (result == PIE_ASM_GEN_OK) {
      return 1;
    }
    if (result == PIE_ASM_GEN_ERROR) {
      return 0;
    }
  }

  api_error(cg, "no ASM codegen hook matched expression");
  return 0;
}

static int emit_expr(PieAsmCodegen *cg, const PieIrExpr *expr);
static int emit_stmt(PieAsmCodegen *cg, const PieIrStmt *stmt);

static const PieAsmCodegenApi PIE_ASM_API = {
    .emit = api_emit,
    .emit_expr = emit_expr,
    .emit_stmt = emit_stmt,
    .add_local = api_add_local,
    .find_local = api_find_local,
    .add_local_at = api_add_local_at,
    .add_string = api_add_string,
    .error = api_error,
    .errorf = api_errorf,
    .diag = api_diag,
    .mark_returned = api_mark_returned,
    .next_label = api_next_label,
    .push_loop = api_push_loop,
    .pop_loop = api_pop_loop,
    .current_loop = api_current_loop,
    .find_loop_label = api_find_loop_label,
    .find_capture = api_find_capture,
    .set_closure_context = api_set_closure_context,
    .in_closure_body = api_in_closure_body,
    .closure_capture_count = api_closure_capture_count,
    .closure_capture_name = api_closure_capture_name,
    .push_frame = api_push_frame,
    .pop_frame = api_pop_frame,
    .set_stack_offset = api_set_stack_offset,
    .emit_deferred = api_emit_deferred};

static const PieAsmCodegenApi *asm_api(void) { return &PIE_ASM_API; }

static int emit_stmt(PieAsmCodegen *cg, const PieIrStmt *stmt) {
  const PieAsmHookRegistry *registry = pie_asm_hook_registry();
  PieAsmCodegenContext ctx;
  ctx.cg = cg;
  ctx.api = asm_api();

  for (size_t i = 0; i < registry->stmt_hook_count; i++) {
    PieAsmGenResult result = registry->stmt_hooks[i].generate(&ctx, stmt);
    if (result == PIE_ASM_GEN_OK) {
      return 1;
    }
    if (result == PIE_ASM_GEN_ERROR) {
      return 0;
    }
  }

  api_error(cg, "no ASM codegen hook matched statement");
  return 0;
}

static void emit_rodata(PieAsmCodegen *cg) {
  api_emit(cg, "section .rodata\n");
  api_emit(cg, "pie_runtime_div_zero: db \"division by zero\", 10\n");
  api_emit(cg, "pie_runtime_mod_zero: db \"modulo by zero\", 10\n");
  api_emit(cg, "pie_bool_true: db \"true\"\n");
  api_emit(cg, "pie_bool_false: db \"false\"\n");
  for (size_t i = 0; i < cg->string_count; i++) {
    const StringLiteral *lit = &cg->strings[i];
    api_emit(cg, "pie_str_%zu: db ", lit->id);
    if (lit->len == 0) {
      api_emit(cg, "0\n");
      continue;
    }
    for (size_t j = 0; j < lit->len; j++) {
      if (j) {
        api_emit(cg, ", ");
      }
      api_emit(cg, "%u", (unsigned char)lit->bytes[j]);
    }
    api_emit(cg, "\n");
  }
}

static void reset_function_frame(PieAsmCodegen *cg) {
  cg->local_count = 0;
  cg->loop_label_count = 0;
  cg->next_stack_offset = 40;
  cg->did_return = 0;
}

static const char *function_arg_reg(size_t index) {
  static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
  return index < 6 ? regs[index] : NULL;
}

static int emit_function_body(PieAsmCodegen *cg, const PieIrProgram *body,
                              const PieIrFunction *function) {
  reset_function_frame(cg);
  api_emit(cg, "    push rbp\n");
  api_emit(cg, "    mov rbp, rsp\n");
  api_emit(cg, "    push rbx\n");
  api_emit(cg, "    push r12\n");
  api_emit(cg, "    push r13\n");
  api_emit(cg, "    push r14\n");
  api_emit(cg, "    push r15\n");
  api_emit(cg, "    sub rsp, 0x1008\n");

  if (body) {
    for (size_t i = 0; i < body->local_count; i++) {
      PieAsmSymbol symbol;
      const PieIrLocal *local = &body->locals[i];
      PieIrTypeKind ref_inner = PIE_IR_TYPE_UNKNOWN;
      int ref_inner_w = PIE_WIDTH_INFER;
      if (local->type == PIE_IR_TYPE_REF ||
          local->type == PIE_IR_TYPE_REF_MUT) {
        ref_inner = local->ref_inner_type;
        ref_inner_w = local->ref_inner_width;
      }
      if (!api_add_local(cg, i, local->is_mut, local->type, local->type_width,
                         ref_inner, ref_inner_w, &symbol)) {
        return 0;
      }
    }
  }

  if (function) {
    size_t arg_slot = 0;
    for (size_t i = 0; i < function->param_count; i++) {
      PieAsmSymbol symbol;
      int pw = function->param_type_widths ? function->param_type_widths[i] : 0;
      PieIrTypeKind ref_inner = PIE_IR_TYPE_UNKNOWN;
      int ref_inner_w = PIE_WIDTH_INFER;
      if (function->param_types[i] == PIE_IR_TYPE_REF ||
          function->param_types[i] == PIE_IR_TYPE_REF_MUT) {
        ref_inner = function->param_ref_inner_types
                        ? function->param_ref_inner_types[i]
                        : PIE_IR_TYPE_UNKNOWN;
        ref_inner_w = function->param_ref_inner_widths
                          ? function->param_ref_inner_widths[i]
                          : PIE_WIDTH_INFER;
      }
      if (!api_add_local(cg, function->param_local_ids[i], 0,
                         function->param_types[i], pw, ref_inner, ref_inner_w,
                         &symbol)) {
        return 0;
      }
      LocalSlot *slot = find_local_internal(cg, function->param_local_ids[i]);
      if (slot) {
        slot->ref_inner_type = ref_inner;
        slot->ref_inner_width = ref_inner_w;
        symbol.ref_inner_type = ref_inner;
        symbol.ref_inner_width = ref_inner_w;
      }
      int is_fat = (function->param_types[i] == PIE_IR_TYPE_REF &&
                    ref_inner == PIE_IR_TYPE_STRING);
      if (function->param_types[i] == PIE_IR_TYPE_FLOAT) {
        static const char *xmm_regs[] = {"xmm0", "xmm1", "xmm2", "xmm3",
                                         "xmm4", "xmm5", "xmm6", "xmm7"};
        if (i >= 8) {
          api_error(cg, "function has more than 8 float parameters");
          return 0;
        }
        if (pw == PIE_WIDTH_32) {
          api_emit(cg, "    movss [rbp-%d], %s\n", symbol.offset, xmm_regs[i]);
        } else {
          api_emit(cg, "    movsd [rbp-%d], %s\n", symbol.offset, xmm_regs[i]);
        }
      } else if (function->param_types[i] == PIE_IR_TYPE_STRING || is_fat) {
        const char *ptr_reg = function_arg_reg(arg_slot);
        const char *len_reg = function_arg_reg(arg_slot + 1);
        if (!ptr_reg || !len_reg) {
          api_error(cg, "function parameter list uses more than 6 linux x64 "
                        "argument register slots; stack-passed parameters are "
                        "not implemented yet");
          return 0;
        }
        api_emit(cg, "    mov [rbp-%d], %s\n", symbol.offset, ptr_reg);
        api_emit(cg, "    mov [rbp-%d], %s\n", symbol.offset - 8, len_reg);
        arg_slot += 2;
      } else {
        const char *reg = function_arg_reg(arg_slot);
        if (!reg) {
          api_error(cg, "function parameter list uses more than 6 linux x64 "
                        "argument register slots; stack-passed parameters are "
                        "not implemented yet");
          return 0;
        }
        api_emit(cg, "    mov [rbp-%d], %s\n", symbol.offset, reg);
        arg_slot++;
      }
    }
  }

  cg->deferred_count = 0;

  for (size_t i = 0; i < body->stmt_count; i++) {
    if (body->stmts[i].kind == PIE_IR_STMT_DEFER) {
      if (body->stmts[i].expr) {
        if (cg->deferred_count >= cg->deferred_capacity) {
          size_t new_cap =
              cg->deferred_capacity ? cg->deferred_capacity * 2 : 8;
          PieIrExpr **tmp = (PieIrExpr **)realloc(
              cg->deferred_exprs, new_cap * sizeof(PieIrExpr *));
          if (tmp) {
            cg->deferred_exprs = tmp;
            cg->deferred_capacity = new_cap;
          }
        }
        if (cg->deferred_exprs && cg->deferred_count < cg->deferred_capacity) {
          cg->deferred_exprs[cg->deferred_count++] = body->stmts[i].expr;
          body->stmts[i].expr = NULL;
        }
      }
      continue;
    }
    if (!emit_stmt(cg, &body->stmts[i])) {
      return 0;
    }
    if (cg->did_return) {
      break;
    }
  }

  if (!cg->did_return) {
    api_emit_deferred(cg);
    api_emit(cg, "    xor rax, rax\n");
    api_emit(cg, "    lea rsp, [rbp-40]\n");
    api_emit(cg, "    pop r15\n");
    api_emit(cg, "    pop r14\n");
    api_emit(cg, "    pop r13\n");
    api_emit(cg, "    pop r12\n");
    api_emit(cg, "    pop rbx\n");
    api_emit(cg, "    pop rbp\n");
    api_emit(cg, "    ret\n");
  }
  return 1;
}

int pie_emit_linux_x64_asm(const PieIrProgram *program, const char *output_path,
                           PieDiagnosticBag *diag) {
  FILE *out = fopen(output_path, "wb");
  if (!out) {
    pie_diag_errorf(diag, "could not open assembly output '%s'", output_path);
    return 0;
  }

  PieAsmCodegen cg;
  memset(&cg, 0, sizeof(cg));
  cg.out = out;
  cg.diag = diag;

  api_emit(&cg, "default rel\n");
  api_emit(&cg, "extern pie_write\n");
  api_emit(&cg, "extern pie_print_int\n");
  api_emit(&cg, "extern pie_print_float\n");
  api_emit(&cg, "extern pie_print_newline\n");
  api_emit(&cg, "extern pie_int_wide_new\n");
  api_emit(&cg, "extern pie_int_wide_free\n");
  api_emit(&cg, "extern pie_int_wide_add\n");
  api_emit(&cg, "extern pie_int_wide_sub\n");
  api_emit(&cg, "extern pie_int_wide_mul\n");
  api_emit(&cg, "extern pie_int_wide_div\n");
  api_emit(&cg, "extern pie_int_wide_mod\n");
  api_emit(&cg, "extern pie_int_wide_neg\n");
  api_emit(&cg, "extern pie_int_wide_cmp\n");
  api_emit(&cg, "extern pie_int_wide_to_i64\n");
  api_emit(&cg, "extern pie_int_wide_print\n");
  api_emit(&cg, "extern pie_float_wide_new\n");
  api_emit(&cg, "extern pie_float_wide_free\n");
  api_emit(&cg, "extern pie_float_wide_add\n");
  api_emit(&cg, "extern pie_float_wide_sub\n");
  api_emit(&cg, "extern pie_float_wide_mul\n");
  api_emit(&cg, "extern pie_float_wide_div\n");
  api_emit(&cg, "extern pie_float_wide_neg\n");
  api_emit(&cg, "extern pie_float_wide_cmp\n");
  api_emit(&cg, "extern pie_float_wide_to_f64\n");
  api_emit(&cg, "extern pie_float_wide_print\n");
  api_emit(&cg, "extern pie_string_concat\n");
  api_emit(&cg, "extern pie_string_eq\n");
  api_emit(&cg, "extern pie_list_push\n");
  api_emit(&cg, "extern pie_list_get\n");
  api_emit(&cg, "extern pie_list_set\n");
  api_emit(&cg, "extern pie_list_len\n");
  api_emit(&cg, "extern pie_list_pop\n");
  api_emit(&cg, "extern pie_list_insert\n");
  api_emit(&cg, "extern pie_list_remove\n");
  api_emit(&cg, "extern pie_list_reverse\n");
  api_emit(&cg, "extern pie_list_sort\n");
  api_emit(&cg, "extern pie_len\n");
  api_emit(&cg, "extern pie_map_create\n");
  api_emit(&cg, "extern pie_map_put\n");
  api_emit(&cg, "extern pie_map_get\n");
  api_emit(&cg, "extern pie_map_len\n");
  api_emit(&cg, "extern pie_int_to_float\n");
  api_emit(&cg, "extern pie_float_to_int\n");
  api_emit(&cg, "extern pie_int_to_string\n");
  api_emit(&cg, "extern pie_float_to_string\n");
  api_emit(&cg, "extern pie_string_to_int\n");
  api_emit(&cg, "extern pie_string_to_float\n");
  api_emit(&cg, "extern pie_string_contains\n");
  api_emit(&cg, "extern pie_string_upper\n");
  api_emit(&cg, "extern pie_string_lower\n");
  api_emit(&cg, "extern pie_string_trim\n");
  api_emit(&cg, "extern pie_string_replace\n");
  api_emit(&cg, "extern pie_string_repeat\n");
  api_emit(&cg, "extern pie_int_power\n");
  api_emit(&cg, "extern pie_float_power\n");
  api_emit(&cg, "extern pie_maybe\n");
  api_emit(&cg, "extern pie_malloc\n");
  api_emit(&cg, "extern pie_free\n");
  api_emit(&cg, "extern pie_thread_spawn\n");
  api_emit(&cg, "extern pie_thread_join\n");
  api_emit(&cg, "extern pie_thread_mutex_create\n");
  api_emit(&cg, "extern pie_thread_mutex_lock\n");
  api_emit(&cg, "extern pie_thread_mutex_unlock\n");
  api_emit(&cg, "extern pie_thread_mutex_destroy\n");
  api_emit(&cg, "extern pie_thread_sleep_ms\n");
  api_emit(&cg, "extern pie_channel_create\n");
  api_emit(&cg, "extern pie_channel_send\n");
  api_emit(&cg, "extern pie_channel_recv\n");
  api_emit(&cg, "extern pie_channel_close\n");
  api_emit(&cg, "extern pie_channel_destroy\n");
  api_emit(&cg, "extern pie_assert_fail\n");
  api_emit(&cg, "extern pie_assert_eq_fail\n");
  api_emit(&cg, "extern pie_string_index_oob\n");
  api_emit(&cg, "extern pie_format_int\n");
  api_emit(&cg, "extern pie_format_float\n");
  api_emit(&cg, "extern pie_format_bool\n");
  api_emit(&cg, "section .text\n");

  for (size_t i = 0; i < program->function_count; i++) {
    api_emit(&cg, "pie_fn_%s:\n", program->functions[i].name);
    if (!emit_function_body(&cg, program->functions[i].body,
                            &program->functions[i])) {
      fclose(out);
      codegen_free(&cg);
      return 0;
    }
  }

  api_emit(&cg, "global pie_main\n");
  api_emit(&cg, "pie_main:\n");
  if (!emit_function_body(&cg, program, NULL)) {
    fclose(out);
    codegen_free(&cg);
    return 0;
  }
  emit_rodata(&cg);

  fclose(out);
  codegen_free(&cg);
  return !diag->has_error;
}
