#include "pie/backend/asm/asm_codegen.h"

#include <stdlib.h>
#include <string.h>

static int ir_type_size(PieIrTypeKind type) {
  if (type == PIE_IR_TYPE_STRING || type == PIE_IR_TYPE_CLOSURE) {
    return 16;
  }
  return 8;
}

PieAsmGenResult pie_feature_structs_codegen_stmt(PieAsmCodegenContext *ctx,
                                                 const PieIrStmt *stmt) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (stmt->kind == PIE_IR_STMT_FIELD_ASSIGN) {
    if (!stmt->field_target || !stmt->field_target->left ||
        !stmt->field_target->field_name) {
      api->error(cg, "invalid field assignment");
      return PIE_ASM_GEN_ERROR;
    }
    if (stmt->field_target->left->kind != PIE_IR_EXPR_LOCAL) {
      api->error(cg, "field assignment only supported on local variables");
      return PIE_ASM_GEN_ERROR;
    }
    PieAsmSymbol obj_sym;
    if (!api->find_local(cg, stmt->field_target->left->local_id, &obj_sym)) {
      api->error(cg, "undefined variable in field assignment");
      return PIE_ASM_GEN_ERROR;
    }
    if (!api->emit_expr(cg, stmt->expr)) {
      return PIE_ASM_GEN_ERROR;
    }

    int val_size = ir_type_size(stmt->expr->type);
    if (val_size == 16) {
      api->emit(cg, "    mov rcx, rax\n");
      api->emit(cg, "    mov rsi, rdx\n");
      api->emit(cg, "    mov rax, [rbp-%d]\n", obj_sym.offset);
      api->emit(cg, "    mov [rax+%d], rcx\n",
                stmt->field_target->field_offset);
      api->emit(cg, "    mov [rax+%d], rsi\n",
                stmt->field_target->field_offset + 8);
    } else if (stmt->expr->type == PIE_IR_TYPE_FLOAT) {
      api->emit(cg, "    mov rax, [rbp-%d]\n", obj_sym.offset);
      api->emit(cg, "    movsd [rax+%d], xmm0\n",
                stmt->field_target->field_offset);
    } else {
      const char *op = stmt->assign_op;
      if (strcmp(op, "=") == 0) {
        api->emit(cg, "    mov rcx, rax\n");
        api->emit(cg, "    mov rax, [rbp-%d]\n", obj_sym.offset);
        api->emit(cg, "    mov [rax+%d], rcx\n",
                  stmt->field_target->field_offset);
      } else {
        api->emit(cg, "    mov rcx, rax\n");
        api->emit(cg, "    mov rbx, [rbp-%d]\n", obj_sym.offset);
        api->emit(cg, "    mov rax, [rbx+%d]\n",
                  stmt->field_target->field_offset);
        if (strcmp(op, "+=") == 0) {
          api->emit(cg, "    add rax, rcx\n");
        } else if (strcmp(op, "-=") == 0) {
          api->emit(cg, "    sub rax, rcx\n");
        } else if (strcmp(op, "*=") == 0) {
          api->emit(cg, "    imul rax, rcx\n");
        } else if (strcmp(op, "/=") == 0) {
          api->emit(cg, "    cqo\n");
          api->emit(cg, "    idiv rcx\n");
        } else if (strcmp(op, "%=") == 0) {
          api->emit(cg, "    cqo\n");
          api->emit(cg, "    idiv rcx\n");
          api->emit(cg, "    mov rax, rdx\n");
        } else {
          api->errorf(cg, "unsupported field assignment operator '%s'", op);
          return PIE_ASM_GEN_ERROR;
        }
        api->emit(cg, "    mov [rbx+%d], rax\n",
                  stmt->field_target->field_offset);
      }
    }
    return PIE_ASM_GEN_OK;
  }

  return PIE_ASM_GEN_NO_MATCH;
}

PieAsmGenResult pie_feature_structs_codegen_expr(PieAsmCodegenContext *ctx,
                                                 const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (expr->kind == PIE_IR_EXPR_NEW) {
    size_t total = 0;
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      total += ir_type_size(expr->call_args[i].expr->type);
    }
    if (total == 0)
      total = 8;

    api->emit(cg, "    push r12\n");
    api->emit(cg, "    sub rsp, 8\n");
    api->emit(cg, "    mov rdi, %zu\n", total);
    api->emit(cg, "    call pie_malloc\n");
    api->emit(cg, "    add rsp, 8\n");
    api->emit(cg, "    mov r12, rax\n");

    size_t offset = 0;
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      if (!api->emit_expr(cg, expr->call_args[i].expr)) {
        return PIE_ASM_GEN_ERROR;
      }
      if (ir_type_size(expr->call_args[i].expr->type) == 16) {
        api->emit(cg, "    mov [r12+%zu], rax\n", offset);
        api->emit(cg, "    mov [r12+%zu], rdx\n", offset + 8);
        offset += 16;
      } else if (expr->call_args[i].expr->type == PIE_IR_TYPE_FLOAT) {
        api->emit(cg, "    movsd [r12+%zu], xmm0\n", offset);
        offset += 8;
      } else {
        api->emit(cg, "    mov [r12+%zu], rax\n", offset);
        offset += 8;
      }
    }
    api->emit(cg, "    mov rax, r12\n");
    api->emit(cg, "    pop r12\n");
    return PIE_ASM_GEN_OK;
  }

  if (expr->kind == PIE_IR_EXPR_FIELD) {
    if (!expr->left || !expr->field_name) {
      api->error(cg, "invalid field access");
      return PIE_ASM_GEN_ERROR;
    }
    if (expr->left->kind == PIE_IR_EXPR_LOCAL) {
      PieAsmSymbol obj_sym;
      if (!api->find_local(cg, expr->left->local_id, &obj_sym)) {
        api->error(cg, "undefined variable in field access");
        return PIE_ASM_GEN_ERROR;
      }
      api->emit(cg, "    mov rax, [rbp-%d]\n", obj_sym.offset);
      if (expr->type == PIE_IR_TYPE_STRING || expr->type == PIE_IR_TYPE_REF) {
        api->emit(cg, "    mov rdx, [rax+%d]\n", expr->field_offset + 8);
        api->emit(cg, "    mov rax, [rax+%d]\n", expr->field_offset);
      } else if (expr->type == PIE_IR_TYPE_FLOAT) {
        api->emit(cg, "    movsd xmm0, [rax+%d]\n", expr->field_offset);
      } else {
        api->emit(cg, "    mov rax, [rax+%d]\n", expr->field_offset);
      }
      return PIE_ASM_GEN_OK;
    }
    if (expr->left->kind == PIE_IR_EXPR_NEW) {
      if (!api->emit_expr(cg, expr->left)) {
        return PIE_ASM_GEN_ERROR;
      }
      if (expr->type == PIE_IR_TYPE_STRING || expr->type == PIE_IR_TYPE_REF) {
        api->emit(cg, "    mov rdx, [rax+%d]\n", expr->field_offset + 8);
        api->emit(cg, "    mov rax, [rax+%d]\n", expr->field_offset);
      } else if (expr->type == PIE_IR_TYPE_FLOAT) {
        api->emit(cg, "    movsd xmm0, [rax+%d]\n", expr->field_offset);
      } else {
        api->emit(cg, "    mov rax, [rax+%d]\n", expr->field_offset);
      }
      return PIE_ASM_GEN_OK;
    }
    if (expr->left->kind == PIE_IR_EXPR_TUPLE) {
      if (!api->emit_expr(cg, expr->left)) {
        return PIE_ASM_GEN_ERROR;
      }
      if (expr->type == PIE_IR_TYPE_STRING || expr->type == PIE_IR_TYPE_REF) {
        api->emit(cg, "    mov rdx, [rax+%d]\n", expr->field_offset + 8);
        api->emit(cg, "    mov rax, [rax+%d]\n", expr->field_offset);
      } else if (expr->type == PIE_IR_TYPE_FLOAT) {
        api->emit(cg, "    movsd xmm0, [rax+%d]\n", expr->field_offset);
      } else {
        api->emit(cg, "    mov rax, [rax+%d]\n", expr->field_offset);
      }
      return PIE_ASM_GEN_OK;
    }
    api->error(
        cg,
        "field access only supported on locals, tuples, and new expressions");
    return PIE_ASM_GEN_ERROR;
  }

  return PIE_ASM_GEN_NO_MATCH;
}
