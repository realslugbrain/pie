#ifndef PIE_CORE_BORROW_BORROWCHECK_H
#define PIE_CORE_BORROW_BORROWCHECK_H

#include "pie/core/ast/ast.h"
#include "pie/core/diag/diag.h"

int pie_borrowcheck_program(const PieProgram *program, PieDiagnosticBag *diag);

#endif
