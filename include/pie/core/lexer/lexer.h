#ifndef PIE_CORE_LEXER_LEXER_H
#define PIE_CORE_LEXER_LEXER_H

#include "pie/core/diag/diag.h"
#include "pie/core/source/source.h"
#include "pie/core/token/token.h"

int pie_lex_source(const PieSource *source, PieTokenList *tokens,
                   PieDiagnosticBag *diag);

#endif
