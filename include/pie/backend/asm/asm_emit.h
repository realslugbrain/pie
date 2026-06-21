#ifndef PIE_BACKEND_ASM_ASM_EMIT_H
#define PIE_BACKEND_ASM_ASM_EMIT_H

#include "pie/core/diag/diag.h"
#include "pie/core/ir/ir.h"

int pie_emit_linux_x64_asm(const PieIrProgram *program, const char *output_path,
                           PieDiagnosticBag *diag);

#endif
