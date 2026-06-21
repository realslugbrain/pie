#ifndef PIE_CORE_MODULES_LOADER_H
#define PIE_CORE_MODULES_LOADER_H

#include "pie/core/ast/ast.h"
#include "pie/core/diag/diag.h"

int pie_load_modules(PieProgram *program, const char *source_path,
                     PieDiagnosticBag *diag);

#endif
