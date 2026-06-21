#include "pie/core/parser/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int token_starts_stmt(PieTokenKind kind) {
  switch (kind) {
  case PIE_TOK_LET:
  case PIE_TOK_MUT:
  case PIE_TOK_IF:
  case PIE_TOK_FOR:
  case PIE_TOK_RETURN:
  case PIE_TOK_DEFER:
  case PIE_TOK_PRINTLN:
  case PIE_TOK_MATCH:
  case PIE_TOK_STRUCT:
  case PIE_TOK_ENUM:
  case PIE_TOK_FN:
  case PIE_TOK_PUB:
  case PIE_TOK_CONST:
    return 1;
  default:
    return 0;
  }
}

PieParseResult pie_feature_enums_parse_top_level(PieParseContext *ctx,
                                                 PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  size_t start = api->pos(parser);
  int is_pub = 0;
  if (api->match(parser, PIE_TOK_PUB)) {
    is_pub = 1;
  }

  if (!api->match(parser, PIE_TOK_ENUM)) {
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }

  const PieToken *name_token = api->peek(parser);
  if (!api->expect(parser, PIE_TOK_IDENTIFIER, "expected enum name")) {
    return PIE_PARSE_ERROR;
  }

  if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after enum name")) {
    return PIE_PARSE_ERROR;
  }

  PieEnumDef def;
  memset(&def, 0, sizeof(def));
  def.name = api->copy_token_text(name_token);
  def.is_pub = is_pub;
  if (!def.name) {
    pie_diag_error(api->diag(parser), "out of memory while storing enum name");
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);

  while (!api->check(parser, PIE_TOK_END) && !api->check(parser, PIE_TOK_EOF)) {
    if (!api->match(parser, PIE_TOK_CASE)) {
      api->error_at(parser, api->peek(parser),
                    "expected 'case' in enum declaration");
      goto enum_error;
    }

    const PieToken *variant_token = api->peek(parser);
    if (!api->expect(parser, PIE_TOK_IDENTIFIER, "expected variant name")) {
      goto enum_error;
    }

    if (def.variant_count >= PIE_ENUM_MAX_VARIANTS) {
      api->error_at(parser, api->peek(parser), "too many enum variants");
      goto enum_error;
    }

    PieEnumVariant *variant = &def.variants[def.variant_count];
    memset(variant, 0, sizeof(*variant));
    variant->name = api->copy_token_text(variant_token);
    if (!variant->name) {
      pie_diag_error(api->diag(parser),
                     "out of memory while storing variant name");
      goto enum_error;
    }

    if (api->match(parser, PIE_TOK_LPAREN)) {
      size_t cap = 4;
      variant->payload_kinds =
          (PieAstTypeKind *)calloc(cap, sizeof(PieAstTypeKind));
      variant->payload_widths = (int *)calloc(cap, sizeof(int));
      if (!variant->payload_kinds || !variant->payload_widths) {
        free(variant->name);
        pie_diag_error(api->diag(parser),
                       "out of memory while storing variant payload");
        goto enum_error;
      }

      while (!api->check(parser, PIE_TOK_RPAREN)) {
        if (variant->payload_count > 0) {
          if (!api->match(parser, PIE_TOK_COMMA)) {
            api->error_at(parser, api->peek(parser),
                          "expected ',' or ')' in variant payload");
            goto enum_error;
          }
        }

        if (api->check(parser, PIE_TOK_INT_TYPE)) {
          api->advance(parser);
          variant->payload_kinds[variant->payload_count] = PIE_AST_TYPE_INT;
        } else if (api->check(parser, PIE_TOK_FLOAT_TYPE)) {
          api->advance(parser);
          variant->payload_kinds[variant->payload_count] = PIE_AST_TYPE_FLOAT;
        } else if (api->check(parser, PIE_TOK_STRING_TYPE)) {
          api->advance(parser);
          variant->payload_kinds[variant->payload_count] = PIE_AST_TYPE_STRING;
        } else if (api->check(parser, PIE_TOK_BOOL_TYPE)) {
          api->advance(parser);
          variant->payload_kinds[variant->payload_count] = PIE_AST_TYPE_BOOL;
        } else if (api->check(parser, PIE_TOK_CHAR_TYPE)) {
          api->advance(parser);
          variant->payload_kinds[variant->payload_count] = PIE_AST_TYPE_CHAR;
        } else if (api->check(parser, PIE_TOK_BYTE_TYPE)) {
          api->advance(parser);
          variant->payload_kinds[variant->payload_count] = PIE_AST_TYPE_BYTE;
        } else {
          api->error_at(parser, api->peek(parser),
                        "expected type in variant payload");
          goto enum_error;
        }

        variant->payload_widths[variant->payload_count] = PIE_WIDTH_INFER;
        variant->payload_count++;

        if (variant->payload_count >= cap) {
          cap *= 2;
          PieAstTypeKind *new_kinds = (PieAstTypeKind *)realloc(
              variant->payload_kinds, cap * sizeof(PieAstTypeKind));
          int *new_widths =
              (int *)realloc(variant->payload_widths, cap * sizeof(int));
          if (!new_kinds || !new_widths) {
            free(variant->name);
            free(variant->payload_kinds);
            free(variant->payload_widths);
            pie_diag_error(api->diag(parser),
                           "out of memory while growing variant payload");
            goto enum_error;
          }
          variant->payload_kinds = new_kinds;
          variant->payload_widths = new_widths;
        }
      }

      if (!api->expect(parser, PIE_TOK_RPAREN,
                       "expected ')' after variant payload")) {
        free(variant->name);
        free(variant->payload_kinds);
        free(variant->payload_widths);
        goto enum_error;
      }
    }

    def.variant_count++;
    api->skip_separators(parser);
  }

  if (!api->expect(parser, PIE_TOK_END,
                   "expected 'end' after enum declaration")) {
    goto enum_error;
  }

  if (!pie_program_push_enum(program, def)) {
    pie_diag_error(api->diag(parser),
                   "out of memory while storing enum definition");
    goto enum_error;
  }

  {
    PieStmt stmt;
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = PIE_STMT_ENUM;
    stmt.enum_def = (PieEnumDef *)malloc(sizeof(PieEnumDef));
    if (!stmt.enum_def) {
      pie_diag_error(api->diag(parser),
                     "out of memory while storing enum statement");
      return PIE_PARSE_ERROR;
    }

    stmt.enum_def->name = (char *)malloc(strlen(def.name) + 1);
    if (stmt.enum_def->name)
      strcpy(stmt.enum_def->name, def.name);
    stmt.enum_def->variant_count = def.variant_count;
    for (size_t i = 0; i < def.variant_count; i++) {
      stmt.enum_def->variants[i].name =
          (char *)malloc(strlen(def.variants[i].name) + 1);
      if (stmt.enum_def->variants[i].name)
        strcpy(stmt.enum_def->variants[i].name, def.variants[i].name);
      stmt.enum_def->variants[i].payload_count = def.variants[i].payload_count;
      if (def.variants[i].payload_count > 0) {
        stmt.enum_def->variants[i].payload_kinds = (PieAstTypeKind *)malloc(
            def.variants[i].payload_count * sizeof(PieAstTypeKind));
        stmt.enum_def->variants[i].payload_widths =
            (int *)malloc(def.variants[i].payload_count * sizeof(int));
        if (stmt.enum_def->variants[i].payload_kinds) {
          memcpy(stmt.enum_def->variants[i].payload_kinds,
                 def.variants[i].payload_kinds,
                 def.variants[i].payload_count * sizeof(PieAstTypeKind));
        }
        if (stmt.enum_def->variants[i].payload_widths) {
          memcpy(stmt.enum_def->variants[i].payload_widths,
                 def.variants[i].payload_widths,
                 def.variants[i].payload_count * sizeof(int));
        }
      } else {
        stmt.enum_def->variants[i].payload_kinds = NULL;
        stmt.enum_def->variants[i].payload_widths = NULL;
      }
    }

    if (!pie_program_push_stmt(program, stmt)) {
      free(stmt.enum_def->name);
      for (size_t i = 0; i < stmt.enum_def->variant_count; i++) {
        free(stmt.enum_def->variants[i].name);
        free(stmt.enum_def->variants[i].payload_kinds);
        free(stmt.enum_def->variants[i].payload_widths);
      }
      free(stmt.enum_def);
      pie_diag_error(api->diag(parser),
                     "out of memory while storing enum statement");
      return PIE_PARSE_ERROR;
    }
  }

  return PIE_PARSE_OK;

enum_error:
  free(def.name);
  for (size_t i = 0; i < def.variant_count; i++) {
    free(def.variants[i].name);
    free(def.variants[i].payload_kinds);
    free(def.variants[i].payload_widths);
  }
  return PIE_PARSE_ERROR;
}

PieParseResult pie_feature_enums_parse_variant_expr(PieParseContext *ctx,
                                                    PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
    return PIE_PARSE_NO_MATCH;
  }

  const PieToken *enum_token = api->peek(parser);
  if (enum_token->len == 0 || enum_token->start[0] < 'A' ||
      enum_token->start[0] > 'Z') {
    return PIE_PARSE_NO_MATCH;
  }

  int is_bare_option =
      (enum_token->len == 4 && memcmp(enum_token->start, "Some", 4) == 0) ||
      (enum_token->len == 4 && memcmp(enum_token->start, "None", 4) == 0);
  int is_bare_result =
      (enum_token->len == 2 && memcmp(enum_token->start, "Ok", 2) == 0) ||
      (enum_token->len == 3 && memcmp(enum_token->start, "Err", 3) == 0);

  if (is_bare_option || is_bare_result) {
    const char *raw_name = is_bare_option ? "Option" : "Result";
    size_t name_len = strlen(raw_name);
    char *enum_name = (char *)malloc(name_len + 1);
    memcpy(enum_name, raw_name, name_len + 1);
    char *variant_name = api->copy_token_text(enum_token);
    api->advance(parser);

    *out_expr = pie_expr_variant(enum_name, variant_name);
    free(enum_name);
    free(variant_name);

    if (api->match(parser, PIE_TOK_LPAREN)) {
      while (!api->check(parser, PIE_TOK_RPAREN)) {
        if ((*out_expr)->call_arg_count > 0) {
          if (!api->match(parser, PIE_TOK_COMMA)) {
            api->error_at(parser, api->peek(parser),
                          "expected ',' or ')' in variant arguments");
            return PIE_PARSE_ERROR;
          }
        }
        PieExpr *arg = api->parse_expr(parser);
        if (!arg) {
          return PIE_PARSE_ERROR;
        }
        if (!pie_expr_call_add_arg(*out_expr, arg)) {
          pie_expr_free(arg);
          return PIE_PARSE_ERROR;
        }
      }
      if (!api->expect(parser, PIE_TOK_RPAREN,
                       "expected ')' after variant arguments")) {
        return PIE_PARSE_ERROR;
      }
    }

    return PIE_PARSE_OK;
  }

  if (api->peek_n(parser, 1)->kind != PIE_TOK_DOT) {
    return PIE_PARSE_NO_MATCH;
  }

  if (api->peek_n(parser, 2)->kind != PIE_TOK_IDENTIFIER) {
    return PIE_PARSE_NO_MATCH;
  }

  api->advance(parser);
  api->advance(parser);

  const PieToken *variant_token = api->peek(parser);
  char *enum_name = api->copy_token_text(enum_token);
  char *variant_name = api->copy_token_text(variant_token);
  api->advance(parser);

  if (!enum_name || !variant_name) {
    free(enum_name);
    free(variant_name);
    return PIE_PARSE_ERROR;
  }

  *out_expr = pie_expr_variant(enum_name, variant_name);
  free(enum_name);
  free(variant_name);

  if (api->match(parser, PIE_TOK_LPAREN)) {
    while (!api->check(parser, PIE_TOK_RPAREN)) {
      if ((*out_expr)->call_arg_count > 0) {
        if (!api->match(parser, PIE_TOK_COMMA)) {
          api->error_at(parser, api->peek(parser),
                        "expected ',' or ')' in variant arguments");
          return PIE_PARSE_ERROR;
        }
      }
      PieExpr *arg = api->parse_expr(parser);
      if (!arg) {
        return PIE_PARSE_ERROR;
      }
      if (!pie_expr_call_add_arg(*out_expr, arg)) {
        pie_expr_free(arg);
        return PIE_PARSE_ERROR;
      }
    }
    if (!api->expect(parser, PIE_TOK_RPAREN,
                     "expected ')' after variant arguments")) {
      return PIE_PARSE_ERROR;
    }
  }

  return PIE_PARSE_OK;
}

static void free_match_cases(PieStmt *stmt) {
  for (size_t i = 0; i < stmt->match_case_count; i++) {
    free(stmt->match_case_names[i]);
    for (size_t j = 0; j < stmt->match_case_binding_counts[i]; j++) {
      free(stmt->match_case_bindings[i][j]);
    }
    free(stmt->match_case_bindings[i]);
    if (stmt->match_case_bodies[i]) {
      pie_program_free(stmt->match_case_bodies[i]);
      free(stmt->match_case_bodies[i]);
    }
  }
  free(stmt->match_case_names);
  free(stmt->match_case_bindings);
  free(stmt->match_case_binding_counts);
  free(stmt->match_case_bodies);
  if (stmt->match_default) {
    pie_program_free(stmt->match_default);
    free(stmt->match_default);
  }
}

PieParseResult pie_feature_enums_parse_match_stmt(PieParseContext *ctx,
                                                  PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_MATCH)) {
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *target = api->parse_expr(parser);
  if (!target) {
    return PIE_PARSE_ERROR;
  }

  if (!api->expect(parser, PIE_TOK_COLON,
                   "expected ':' after match expression")) {
    pie_expr_free(target);
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_MATCH;
  stmt.match_target = target;

  size_t case_cap = 8;
  stmt.match_case_names = (char **)calloc(case_cap, sizeof(char *));
  stmt.match_case_bodies =
      (PieProgram **)calloc(case_cap, sizeof(PieProgram *));
  stmt.match_case_bindings = (char ***)calloc(case_cap, sizeof(char **));
  stmt.match_case_binding_counts = (size_t *)calloc(case_cap, sizeof(size_t));

  api->skip_separators(parser);

  while (!api->check(parser, PIE_TOK_END) && !api->check(parser, PIE_TOK_EOF)) {
    if (api->match(parser, PIE_TOK_CASE)) {
      int is_bare = 0;
      if (api->check(parser, PIE_TOK_IDENTIFIER)) {
        const PieToken *check = api->peek(parser);
        is_bare = (check->len == 4 && memcmp(check->start, "Some", 4) == 0) ||
                  (check->len == 4 && memcmp(check->start, "None", 4) == 0) ||
                  (check->len == 2 && memcmp(check->start, "Ok", 2) == 0) ||
                  (check->len == 3 && memcmp(check->start, "Err", 3) == 0);
      }

      if (is_bare) {
        const PieToken *variant_tok = api->peek(parser);
        char *variant_name = api->copy_token_text(variant_tok);
        int is_option =
            (variant_name[0] == 'S' || strcmp(variant_name, "None") == 0);
        const char *raw_enum = is_option ? "Option" : "Result";
        size_t enum_len = strlen(raw_enum);
        char *enum_name = (char *)malloc(enum_len + 1);
        memcpy(enum_name, raw_enum, enum_len + 1);
        api->advance(parser);

        size_t case_name_len = strlen(enum_name) + 1 + strlen(variant_name);
        char *case_name = (char *)malloc(case_name_len + 1);
        memcpy(case_name, enum_name, strlen(enum_name));
        case_name[strlen(enum_name)] = '.';
        memcpy(case_name + strlen(enum_name) + 1, variant_name,
               strlen(variant_name) + 1);

        free(enum_name);
        free(variant_name);

        if (stmt.match_case_count >= case_cap) {
          case_cap *= 2;
          char **new_names = (char **)realloc(stmt.match_case_names,
                                              case_cap * sizeof(char *));
          PieProgram **new_bodies = (PieProgram **)realloc(
              stmt.match_case_bodies, case_cap * sizeof(PieProgram *));
          char ***new_bindings = (char ***)realloc(stmt.match_case_bindings,
                                                   case_cap * sizeof(char **));
          size_t *new_binding_counts = (size_t *)realloc(
              stmt.match_case_binding_counts, case_cap * sizeof(size_t));
          if (!new_names || !new_bodies || !new_bindings ||
              !new_binding_counts) {
            free(case_name);
            pie_diag_error(api->diag(parser),
                           "out of memory while growing match cases");
            goto match_error;
          }
          stmt.match_case_names = new_names;
          stmt.match_case_bodies = new_bodies;
          stmt.match_case_bindings = new_bindings;
          stmt.match_case_binding_counts = new_binding_counts;
        }

        stmt.match_case_names[stmt.match_case_count] = case_name;

        if (api->match(parser, PIE_TOK_LPAREN)) {
          size_t bind_cap = 4;
          char **bindings = (char **)calloc(bind_cap, sizeof(char *));
          size_t bind_count = 0;

          while (!api->check(parser, PIE_TOK_RPAREN)) {
            if (bind_count > 0) {
              if (!api->match(parser, PIE_TOK_COMMA)) {
                api->error_at(parser, api->peek(parser),
                              "expected ',' or ')' in match bindings");
                for (size_t j = 0; j < bind_count; j++)
                  free(bindings[j]);
                free(bindings);
                goto match_error;
              }
            }

            if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
              api->error_at(parser, api->peek(parser),
                            "expected binding name in match case");
              for (size_t j = 0; j < bind_count; j++)
                free(bindings[j]);
              free(bindings);
              goto match_error;
            }

            if (bind_count >= bind_cap) {
              bind_cap *= 2;
              char **new_bindings =
                  (char **)realloc(bindings, bind_cap * sizeof(char *));
              if (!new_bindings) {
                for (size_t j = 0; j < bind_count; j++)
                  free(bindings[j]);
                free(bindings);
                pie_diag_error(api->diag(parser),
                               "out of memory while growing match bindings");
                goto match_error;
              }
              bindings = new_bindings;
            }

            const PieToken *bind_tok = api->advance(parser);
            bindings[bind_count] = api->copy_token_text(bind_tok);
            bind_count++;
          }

          if (!api->expect(parser, PIE_TOK_RPAREN,
                           "expected ')' after match bindings")) {
            for (size_t j = 0; j < bind_count; j++)
              free(bindings[j]);
            free(bindings);
            goto match_error;
          }

          stmt.match_case_bindings[stmt.match_case_count] = bindings;
          stmt.match_case_binding_counts[stmt.match_case_count] = bind_count;
        } else {
          stmt.match_case_bindings[stmt.match_case_count] = NULL;
          stmt.match_case_binding_counts[stmt.match_case_count] = 0;
        }

        PieProgram *body = (PieProgram *)malloc(sizeof(PieProgram));
        if (!body) {
          pie_diag_error(api->diag(parser),
                         "out of memory while creating match case body");
          goto match_error;
        }
        pie_program_init(body);
        if (!api->expect(parser, PIE_TOK_COLON,
                         "expected ':' after match case")) {
          pie_program_free(body);
          free(body);
          goto match_error;
        }
        api->skip_separators(parser);
        while (!api->check(parser, PIE_TOK_CASE) &&
               !api->check(parser, PIE_TOK_END) &&
               !api->check(parser, PIE_TOK_EOF)) {
          if (!api->parse_statement(parser, body)) {
            pie_program_free(body);
            free(body);
            goto match_error;
          }
          api->skip_separators(parser);
        }
        stmt.match_case_bodies[stmt.match_case_count] = body;
        stmt.match_case_count++;

      } else if (api->check(parser, PIE_TOK_IDENTIFIER) &&
                 api->peek_n(parser, 1)->kind == PIE_TOK_DOT) {
        const PieToken *enum_tok = api->advance(parser);
        api->advance(parser);
        const PieToken *variant_tok = api->peek(parser);
        if (!api->expect(parser, PIE_TOK_IDENTIFIER, "expected variant name")) {
          goto match_error;
        }

        size_t name_len = enum_tok->len + 1 + variant_tok->len;
        char *case_name = (char *)malloc(name_len + 1);
        if (!case_name) {
          pie_diag_error(api->diag(parser),
                         "out of memory while storing case name");
          goto match_error;
        }
        memcpy(case_name, enum_tok->start, enum_tok->len);
        case_name[enum_tok->len] = '.';
        memcpy(case_name + enum_tok->len + 1, variant_tok->start,
               variant_tok->len);
        case_name[name_len] = '\0';

        if (stmt.match_case_count >= case_cap) {
          case_cap *= 2;
          char **new_names = (char **)realloc(stmt.match_case_names,
                                              case_cap * sizeof(char *));
          PieProgram **new_bodies = (PieProgram **)realloc(
              stmt.match_case_bodies, case_cap * sizeof(PieProgram *));
          char ***new_bindings = (char ***)realloc(stmt.match_case_bindings,
                                                   case_cap * sizeof(char **));
          size_t *new_binding_counts = (size_t *)realloc(
              stmt.match_case_binding_counts, case_cap * sizeof(size_t));
          if (!new_names || !new_bodies || !new_bindings ||
              !new_binding_counts) {
            free(case_name);
            pie_diag_error(api->diag(parser),
                           "out of memory while growing match cases");
            goto match_error;
          }
          stmt.match_case_names = new_names;
          stmt.match_case_bodies = new_bodies;
          stmt.match_case_bindings = new_bindings;
          stmt.match_case_binding_counts = new_binding_counts;
        }

        stmt.match_case_names[stmt.match_case_count] = case_name;

        if (api->match(parser, PIE_TOK_LPAREN)) {
          size_t bind_cap = 4;
          char **bindings = (char **)calloc(bind_cap, sizeof(char *));
          size_t bind_count = 0;

          while (!api->check(parser, PIE_TOK_RPAREN)) {
            if (bind_count > 0) {
              if (!api->match(parser, PIE_TOK_COMMA)) {
                api->error_at(parser, api->peek(parser),
                              "expected ',' or ')' in match bindings");
                for (size_t j = 0; j < bind_count; j++)
                  free(bindings[j]);
                free(bindings);
                goto match_error;
              }
            }

            if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
              api->error_at(parser, api->peek(parser),
                            "expected binding name in match case");
              for (size_t j = 0; j < bind_count; j++)
                free(bindings[j]);
              free(bindings);
              goto match_error;
            }

            const PieToken *bind_tok = api->advance(parser);
            char *bind_name = api->copy_token_text(bind_tok);
            if (!bind_name) {
              for (size_t j = 0; j < bind_count; j++)
                free(bindings[j]);
              free(bindings);
              pie_diag_error(api->diag(parser),
                             "out of memory while storing binding name");
              goto match_error;
            }

            if (bind_count >= bind_cap) {
              bind_cap *= 2;
              char **new_bindings =
                  (char **)realloc(bindings, bind_cap * sizeof(char *));
              if (!new_bindings) {
                free(bind_name);
                for (size_t j = 0; j < bind_count; j++)
                  free(bindings[j]);
                free(bindings);
                pie_diag_error(api->diag(parser),
                               "out of memory while growing bindings");
                goto match_error;
              }
              bindings = new_bindings;
            }

            bindings[bind_count++] = bind_name;
          }

          if (!api->expect(parser, PIE_TOK_RPAREN,
                           "expected ')' after match bindings")) {
            for (size_t j = 0; j < bind_count; j++)
              free(bindings[j]);
            free(bindings);
            goto match_error;
          }

          stmt.match_case_bindings[stmt.match_case_count] = bindings;
          stmt.match_case_binding_counts[stmt.match_case_count] = bind_count;
        } else {
          stmt.match_case_bindings[stmt.match_case_count] = NULL;
          stmt.match_case_binding_counts[stmt.match_case_count] = 0;
        }

        if (!api->expect(parser, PIE_TOK_COLON,
                         "expected ':' after case pattern")) {
          goto match_error;
        }

        PieProgram *body = (PieProgram *)malloc(sizeof(PieProgram));
        if (!body) {
          pie_diag_error(api->diag(parser),
                         "out of memory while parsing match case body");
          goto match_error;
        }
        pie_program_init(body);

        api->skip_separators(parser);
        while (!api->check(parser, PIE_TOK_CASE) &&
               !api->check(parser, PIE_TOK_END) &&
               !api->check(parser, PIE_TOK_EOF)) {
          if (!api->parse_statement(parser, body)) {
            pie_program_free(body);
            free(body);
            goto match_error;
          }
          api->skip_separators(parser);
        }

        stmt.match_case_bodies[stmt.match_case_count] = body;
        stmt.match_case_count++;
      } else if ((api->check(parser, PIE_TOK_IDENTIFIER) &&
                  memcmp(api->peek(parser)->start, "_", 1) == 0 &&
                  api->peek(parser)->len == 1) ||
                 api->check(parser, PIE_TOK_ELSE)) {
        if (api->check(parser, PIE_TOK_ELSE)) {
          api->advance(parser);
        } else {
          api->advance(parser);
        }

        if (!api->expect(parser, PIE_TOK_COLON,
                         "expected ':' after default case")) {
          goto match_error;
        }

        if (stmt.match_default) {
          api->error_at(parser, api->peek(parser),
                        "duplicate default case in match");
          goto match_error;
        }

        PieProgram *body = (PieProgram *)malloc(sizeof(PieProgram));
        if (!body) {
          pie_diag_error(api->diag(parser),
                         "out of memory while parsing default case body");
          goto match_error;
        }
        pie_program_init(body);

        api->skip_separators(parser);
        while (!api->check(parser, PIE_TOK_CASE) &&
               !api->check(parser, PIE_TOK_END) &&
               !api->check(parser, PIE_TOK_EOF)) {
          if (!api->parse_statement(parser, body)) {
            pie_program_free(body);
            free(body);
            goto match_error;
          }
          api->skip_separators(parser);
        }

        stmt.match_default = body;
      } else {
        api->error_at(parser, api->peek(parser),
                      "expected variant pattern or '_' in match case");
        goto match_error;
      }
    } else {
      api->error_at(parser, api->peek(parser),
                    "expected 'case' in match statement");
      goto match_error;
    }
    api->skip_separators(parser);
  }

  if (!api->expect(parser, PIE_TOK_END,
                   "expected 'end' after match statement")) {
    goto match_error;
  }

  if (!pie_program_push_stmt(program, stmt)) {
    free_match_cases(&stmt);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing match statement");
    return PIE_PARSE_ERROR;
  }

  return PIE_PARSE_OK;

match_error:
  pie_expr_free(stmt.match_target);
  free_match_cases(&stmt);
  return PIE_PARSE_ERROR;
}

static void free_match_expr_cases(PieExpr *expr) {
  for (size_t i = 0; i < expr->match_expr_case_count; i++) {
    free(expr->match_expr_case_names[i]);
    for (size_t j = 0; j < expr->match_expr_case_binding_counts[i]; j++) {
      free(expr->match_expr_case_bindings[i][j]);
    }
    free(expr->match_expr_case_bindings[i]);
    if (expr->match_expr_case_bodies[i]) {
      pie_program_free(expr->match_expr_case_bodies[i]);
      free(expr->match_expr_case_bodies[i]);
    }
  }
  free(expr->match_expr_case_names);
  free(expr->match_expr_case_bindings);
  free(expr->match_expr_case_binding_counts);
  free(expr->match_expr_case_bodies);
  if (expr->match_expr_default) {
    pie_program_free(expr->match_expr_default);
    free(expr->match_expr_default);
  }
}

PieParseResult pie_feature_enums_parse_match_expr(PieParseContext *ctx,
                                                  PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_MATCH)) {
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *target = api->parse_expr(parser);
  if (!target) {
    return PIE_PARSE_ERROR;
  }

  if (!api->expect(parser, PIE_TOK_COLON,
                   "expected ':' after match expression")) {
    pie_expr_free(target);
    return PIE_PARSE_ERROR;
  }

  PieExpr *expr = pie_expr_match(target);
  if (!expr) {
    pie_expr_free(target);
    return PIE_PARSE_ERROR;
  }

  size_t case_cap = 8;
  expr->match_expr_case_names = (char **)calloc(case_cap, sizeof(char *));
  expr->match_expr_case_bodies =
      (PieProgram **)calloc(case_cap, sizeof(PieProgram *));
  expr->match_expr_case_bindings = (char ***)calloc(case_cap, sizeof(char **));
  expr->match_expr_case_binding_counts =
      (size_t *)calloc(case_cap, sizeof(size_t));

  api->skip_separators(parser);

  while (!api->check(parser, PIE_TOK_END) && !api->check(parser, PIE_TOK_EOF)) {
    if (api->match(parser, PIE_TOK_CASE)) {
      if (api->check(parser, PIE_TOK_IDENTIFIER) &&
          api->peek_n(parser, 1)->kind == PIE_TOK_DOT) {
        const PieToken *enum_tok = api->advance(parser);
        api->advance(parser);
        const PieToken *variant_tok = api->peek(parser);
        if (!api->expect(parser, PIE_TOK_IDENTIFIER, "expected variant name")) {
          goto match_expr_error;
        }

        size_t name_len = enum_tok->len + 1 + variant_tok->len;
        char *case_name = (char *)malloc(name_len + 1);
        if (!case_name) {
          pie_diag_error(api->diag(parser),
                         "out of memory while storing case name");
          goto match_expr_error;
        }
        memcpy(case_name, enum_tok->start, enum_tok->len);
        case_name[enum_tok->len] = '.';
        memcpy(case_name + enum_tok->len + 1, variant_tok->start,
               variant_tok->len);
        case_name[name_len] = '\0';

        if (expr->match_expr_case_count >= case_cap) {
          case_cap *= 2;
          char **new_names = (char **)realloc(expr->match_expr_case_names,
                                              case_cap * sizeof(char *));
          PieProgram **new_bodies = (PieProgram **)realloc(
              expr->match_expr_case_bodies, case_cap * sizeof(PieProgram *));
          char ***new_bindings = (char ***)realloc(
              expr->match_expr_case_bindings, case_cap * sizeof(char **));
          size_t *new_binding_counts = (size_t *)realloc(
              expr->match_expr_case_binding_counts, case_cap * sizeof(size_t));
          if (!new_names || !new_bodies || !new_bindings ||
              !new_binding_counts) {
            free(case_name);
            pie_diag_error(api->diag(parser),
                           "out of memory while growing match cases");
            goto match_expr_error;
          }
          expr->match_expr_case_names = new_names;
          expr->match_expr_case_bodies = new_bodies;
          expr->match_expr_case_bindings = new_bindings;
          expr->match_expr_case_binding_counts = new_binding_counts;
        }

        expr->match_expr_case_names[expr->match_expr_case_count] = case_name;

        if (api->match(parser, PIE_TOK_LPAREN)) {
          size_t bind_cap = 4;
          char **bindings = (char **)calloc(bind_cap, sizeof(char *));
          size_t bind_count = 0;

          while (!api->check(parser, PIE_TOK_RPAREN)) {
            if (bind_count > 0) {
              if (!api->match(parser, PIE_TOK_COMMA)) {
                api->error_at(parser, api->peek(parser),
                              "expected ',' or ')' in match bindings");
                for (size_t j = 0; j < bind_count; j++)
                  free(bindings[j]);
                free(bindings);
                goto match_expr_error;
              }
            }

            if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
              api->error_at(parser, api->peek(parser),
                            "expected binding name in match case");
              for (size_t j = 0; j < bind_count; j++)
                free(bindings[j]);
              free(bindings);
              goto match_expr_error;
            }

            const PieToken *bind_tok = api->advance(parser);
            char *bind_name = api->copy_token_text(bind_tok);
            if (!bind_name) {
              for (size_t j = 0; j < bind_count; j++)
                free(bindings[j]);
              free(bindings);
              pie_diag_error(api->diag(parser),
                             "out of memory while storing binding name");
              goto match_expr_error;
            }

            if (bind_count >= bind_cap) {
              bind_cap *= 2;
              char **new_bindings =
                  (char **)realloc(bindings, bind_cap * sizeof(char *));
              if (!new_bindings) {
                free(bind_name);
                for (size_t j = 0; j < bind_count; j++)
                  free(bindings[j]);
                free(bindings);
                pie_diag_error(api->diag(parser),
                               "out of memory while growing bindings");
                goto match_expr_error;
              }
              bindings = new_bindings;
            }

            bindings[bind_count++] = bind_name;
          }

          if (!api->expect(parser, PIE_TOK_RPAREN,
                           "expected ')' after match bindings")) {
            for (size_t j = 0; j < bind_count; j++)
              free(bindings[j]);
            free(bindings);
            goto match_expr_error;
          }

          expr->match_expr_case_bindings[expr->match_expr_case_count] =
              bindings;
          expr->match_expr_case_binding_counts[expr->match_expr_case_count] =
              bind_count;
        } else {
          expr->match_expr_case_bindings[expr->match_expr_case_count] = NULL;
          expr->match_expr_case_binding_counts[expr->match_expr_case_count] = 0;
        }

        if (!api->expect(parser, PIE_TOK_COLON,
                         "expected ':' after case pattern")) {
          goto match_expr_error;
        }

        PieProgram *body = (PieProgram *)malloc(sizeof(PieProgram));
        if (!body) {
          pie_diag_error(api->diag(parser),
                         "out of memory while parsing match case body");
          goto match_expr_error;
        }
        pie_program_init(body);

        api->skip_separators(parser);
        while (!api->check(parser, PIE_TOK_CASE) &&
               !api->check(parser, PIE_TOK_END) &&
               !api->check(parser, PIE_TOK_EOF)) {
          if (token_starts_stmt(api->peek(parser)->kind)) {
            if (!api->parse_statement(parser, body)) {
              pie_program_free(body);
              free(body);
              goto match_expr_error;
            }
          } else {
            PieExpr *val_expr = api->parse_expr(parser);
            if (val_expr) {
              PieStmt val_stmt;
              memset(&val_stmt, 0, sizeof(val_stmt));
              val_stmt.kind = PIE_STMT_EXPR;
              val_stmt.expr = val_expr;
              pie_program_push_stmt(body, val_stmt);
            } else {
              pie_program_free(body);
              free(body);
              goto match_expr_error;
            }
          }
          api->skip_separators(parser);
        }

        expr->match_expr_case_bodies[expr->match_expr_case_count] = body;
        expr->match_expr_case_count++;
      } else if ((api->check(parser, PIE_TOK_IDENTIFIER) &&
                  memcmp(api->peek(parser)->start, "_", 1) == 0 &&
                  api->peek(parser)->len == 1) ||
                 api->check(parser, PIE_TOK_ELSE)) {
        if (api->check(parser, PIE_TOK_ELSE)) {
          api->advance(parser);
        } else {
          api->advance(parser);
        }

        if (!api->expect(parser, PIE_TOK_COLON,
                         "expected ':' after default case")) {
          goto match_expr_error;
        }

        if (expr->match_expr_default) {
          api->error_at(parser, api->peek(parser),
                        "duplicate default case in match");
          goto match_expr_error;
        }

        PieProgram *body = (PieProgram *)malloc(sizeof(PieProgram));
        if (!body) {
          pie_diag_error(api->diag(parser),
                         "out of memory while parsing default case body");
          goto match_expr_error;
        }
        pie_program_init(body);

        api->skip_separators(parser);
        while (!api->check(parser, PIE_TOK_CASE) &&
               !api->check(parser, PIE_TOK_END) &&
               !api->check(parser, PIE_TOK_EOF)) {
          if (token_starts_stmt(api->peek(parser)->kind)) {
            if (!api->parse_statement(parser, body)) {
              pie_program_free(body);
              free(body);
              goto match_expr_error;
            }
          } else {
            PieExpr *val_expr = api->parse_expr(parser);
            if (val_expr) {
              PieStmt val_stmt;
              memset(&val_stmt, 0, sizeof(val_stmt));
              val_stmt.kind = PIE_STMT_EXPR;
              val_stmt.expr = val_expr;
              pie_program_push_stmt(body, val_stmt);
            } else {
              pie_program_free(body);
              free(body);
              goto match_expr_error;
            }
          }
          api->skip_separators(parser);
        }

        expr->match_expr_default = body;
      } else {
        api->error_at(parser, api->peek(parser),
                      "expected variant pattern or '_' in match case");
        goto match_expr_error;
      }
    } else {
      api->error_at(parser, api->peek(parser),
                    "expected 'case' in match expression");
      goto match_expr_error;
    }
    api->skip_separators(parser);
  }

  if (!api->expect(parser, PIE_TOK_END,
                   "expected 'end' after match expression")) {
    goto match_expr_error;
  }

  *out_expr = expr;
  return PIE_PARSE_OK;

match_expr_error:
  pie_expr_free(expr->match_expr_target);
  free_match_expr_cases(expr);
  free(expr);
  return PIE_PARSE_ERROR;
}

PieParseResult pie_feature_enums_parse_question_postfix(PieParseContext *ctx,
                                                        PieExpr **left,
                                                        int min_precedence) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (min_precedence > 40) {
    return PIE_PARSE_NO_MATCH;
  }

  if (!api->check(parser, PIE_TOK_QUESTION)) {
    return PIE_PARSE_NO_MATCH;
  }

  const PieToken *next = api->peek_n(parser, 1);
  if (next->kind == PIE_TOK_COLON || next->kind == PIE_TOK_EQ) {
    return PIE_PARSE_NO_MATCH;
  }

  if (next->kind == PIE_TOK_INT_LITERAL ||
      next->kind == PIE_TOK_FLOAT_LITERAL ||
      next->kind == PIE_TOK_STRING_LITERAL || next->kind == PIE_TOK_TRUE ||
      next->kind == PIE_TOK_FALSE || next->kind == PIE_TOK_NULL ||
      next->kind == PIE_TOK_LPAREN || next->kind == PIE_TOK_LBRACKET ||
      next->kind == PIE_TOK_LBRACE || next->kind == PIE_TOK_MINUS ||
      next->kind == PIE_TOK_BANG) {
    return PIE_PARSE_NO_MATCH;
  }

  if (next->kind == PIE_TOK_IDENTIFIER) {
    return PIE_PARSE_NO_MATCH;
  }

  api->advance(parser);

  PieExpr *expr = pie_expr_try(*left);
  if (!expr) {
    return PIE_PARSE_ERROR;
  }
  *left = expr;
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_enums_parse_try_expr(PieParseContext *ctx,
                                                PieExpr **out_expr) {
  (void)ctx;
  (void)out_expr;
  return PIE_PARSE_NO_MATCH;
}
