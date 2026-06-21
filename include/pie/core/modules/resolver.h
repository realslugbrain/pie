#ifndef PIE_CORE_MODULES_RESOLVER_H
#define PIE_CORE_MODULES_RESOLVER_H

#include "pie/core/ast/ast.h"
#include "pie/core/diag/diag.h"

int pie_resolve_requires(const PieProgram *program, const char *source_path,
                         PieDiagnosticBag *diag);

#endif
