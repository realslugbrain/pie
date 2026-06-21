#!/usr/bin/env python3
import argparse
import json
import pathlib


def token_name(group, name):
    return "PIE_TOK_" + f"{group}_{name}".upper().replace("-", "_")


def ast_name(group, name):
    return "PIE_AST_" + f"{group}_{name}".upper().replace("-", "_")


def symbol_prefix(group, name):
    return f"pie_feature_{group}_{name}".replace("-", "_")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("group")
    parser.add_argument("name")
    parser.add_argument("--kind", default="feature", choices=["feature", "stmt", "expr", "type", "operator"])
    parser.add_argument("--root", default="src/features")
    args = parser.parse_args()

    feature_dir = pathlib.Path(args.root) / args.group / args.name
    feature_dir.mkdir(parents=True, exist_ok=False)
    prefix = symbol_prefix(args.group, args.name)
    manifest = {
        "id": f"{args.group}.{args.name}",
        "name": args.name.replace("_", " ").title(),
        "group": args.group,
        "version": "0.1.0",
        "kind": args.kind,
        "deps": [],
        "tokens": [token_name(args.group, args.name)],
        "keywords": [],
        "operators": [],
        "ast_nodes": [ast_name(args.group, args.name)],
        "type_kinds": [],
        "hooks": ["parse", "sema", "lower", "codegen"],
    }
    if args.kind == "stmt":
        manifest["parse_hooks"] = {"stmt": [f"{prefix}_parse_stmt"]}
        manifest["sema_hooks"] = {"stmt": [f"{prefix}_sema_stmt"]}
        manifest["lower_hooks"] = {"stmt": [f"{prefix}_lower_stmt"]}
        manifest["asm_hooks"] = {"stmt": [f"{prefix}_codegen_stmt"]}
    elif args.kind == "expr":
        manifest["parse_hooks"] = {"expr_prefix": [f"{prefix}_parse_expr"]}
        manifest["sema_hooks"] = {"expr": [f"{prefix}_sema_expr"]}
        manifest["lower_hooks"] = {"expr": [f"{prefix}_lower_expr"]}
        manifest["asm_hooks"] = {"expr": [f"{prefix}_codegen_expr"]}
    elif args.kind == "operator":
        manifest["parse_hooks"] = {"expr_infix": [f"{prefix}_parse_infix_expr"]}
        manifest["sema_hooks"] = {"expr": [f"{prefix}_sema_expr"]}
        manifest["lower_hooks"] = {"expr": [f"{prefix}_lower_expr"]}
        manifest["asm_hooks"] = {"expr": [f"{prefix}_codegen_expr"]}
    (feature_dir / "feature.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (feature_dir / "README.md").write_text(
        f"# {manifest['name']}\n\n"
        f"Feature capsule for `{manifest['id']}`.\n\n"
    )
    (feature_dir / "syntax.syn").write_text(
        f"# Syntax declarations for {manifest['id']}.\n"
    )
    if args.kind == "stmt":
        parse_body = (
            f"PieParseResult {prefix}_parse_stmt(PieParseContext *ctx, PieProgram *program) {{\n"
            "    (void)ctx;\n"
            "    (void)program;\n"
            "    return PIE_PARSE_NO_MATCH;\n"
            "}\n"
        )
        codegen_body = (
            f"PieAsmGenResult {prefix}_codegen_stmt(PieAsmCodegenContext *ctx, const PieIrStmt *stmt) {{\n"
            "    (void)ctx;\n"
            "    (void)stmt;\n"
            "    return PIE_ASM_GEN_NO_MATCH;\n"
            "}\n"
        )
        sema_body = (
            f"PieSemaResult {prefix}_sema_stmt(PieSemaContext *ctx, const PieStmt *stmt) {{\n"
            "    (void)ctx;\n"
            "    (void)stmt;\n"
            "    return PIE_SEMA_NO_MATCH;\n"
            "}\n"
        )
        lower_body = (
            f"PieLowerResult {prefix}_lower_stmt(PieLowerContext *ctx, const PieStmt *stmt) {{\n"
            "    (void)ctx;\n"
            "    (void)stmt;\n"
            "    return PIE_LOWER_NO_MATCH;\n"
            "}\n"
        )
    elif args.kind == "expr":
        parse_body = (
            f"PieParseResult {prefix}_parse_expr(PieParseContext *ctx, PieExpr **out_expr) {{\n"
            "    (void)ctx;\n"
            "    (void)out_expr;\n"
            "    return PIE_PARSE_NO_MATCH;\n"
            "}\n"
        )
        codegen_body = (
            f"PieAsmGenResult {prefix}_codegen_expr(PieAsmCodegenContext *ctx, const PieIrExpr *expr) {{\n"
            "    (void)ctx;\n"
            "    (void)expr;\n"
            "    return PIE_ASM_GEN_NO_MATCH;\n"
            "}\n"
        )
        sema_body = (
            f"PieSemaResult {prefix}_sema_expr(PieSemaContext *ctx, const PieExpr *expr, PieType *out_type) {{\n"
            "    (void)ctx;\n"
            "    (void)expr;\n"
            "    (void)out_type;\n"
            "    return PIE_SEMA_NO_MATCH;\n"
            "}\n"
        )
        lower_body = (
            f"PieLowerResult {prefix}_lower_expr(PieLowerContext *ctx, const PieExpr *expr, PieIrExpr **out_expr) {{\n"
            "    (void)ctx;\n"
            "    (void)expr;\n"
            "    (void)out_expr;\n"
            "    return PIE_LOWER_NO_MATCH;\n"
            "}\n"
        )
    elif args.kind == "operator":
        parse_body = (
            f"PieParseResult {prefix}_parse_infix_expr(PieParseContext *ctx, PieExpr **left, int min_precedence) {{\n"
            "    (void)ctx;\n"
            "    (void)left;\n"
            "    (void)min_precedence;\n"
            "    return PIE_PARSE_NO_MATCH;\n"
            "}\n"
        )
        codegen_body = (
            f"PieAsmGenResult {prefix}_codegen_expr(PieAsmCodegenContext *ctx, const PieIrExpr *expr) {{\n"
            "    (void)ctx;\n"
            "    (void)expr;\n"
            "    return PIE_ASM_GEN_NO_MATCH;\n"
            "}\n"
        )
        sema_body = (
            f"PieSemaResult {prefix}_sema_expr(PieSemaContext *ctx, const PieExpr *expr, PieType *out_type) {{\n"
            "    (void)ctx;\n"
            "    (void)expr;\n"
            "    (void)out_type;\n"
            "    return PIE_SEMA_NO_MATCH;\n"
            "}\n"
        )
        lower_body = (
            f"PieLowerResult {prefix}_lower_expr(PieLowerContext *ctx, const PieExpr *expr, PieIrExpr **out_expr) {{\n"
            "    (void)ctx;\n"
            "    (void)expr;\n"
            "    (void)out_expr;\n"
            "    return PIE_LOWER_NO_MATCH;\n"
            "}\n"
        )
    else:
        parse_body = (
            f"int {prefix}_parse(void) {{\n"
            "    return 1;\n"
            "}\n"
        )
        codegen_body = (
            f"int {prefix}_codegen(void) {{\n"
            "    return 1;\n"
            "}\n"
        )
        sema_body = (
            f"int {prefix}_sema(void) {{\n"
            "    return 1;\n"
            "}\n"
        )
        lower_body = (
            f"int {prefix}_lower(void) {{\n"
            "    return 1;\n"
            "}\n"
        )

    (feature_dir / "parse.c").write_text(
        f"/* Feature capsule parser for {manifest['id']}. */\n"
        "#include \"pie/core/parser/parser.h\"\n\n"
        f"{parse_body}"
    )
    (feature_dir / "codegen.c").write_text(
        f"/* Feature capsule ASM codegen for {manifest['id']}. */\n"
        "#include \"pie/backend/asm/asm_codegen.h\"\n\n"
        f"{codegen_body}"
    )
    (feature_dir / "sema.c").write_text(
        f"/* Feature capsule sema for {manifest['id']}. */\n"
        "#include \"pie/core/sema/sema.h\"\n\n"
        f"{sema_body}"
    )
    (feature_dir / "lower.c").write_text(
        f"/* Feature capsule lowering for {manifest['id']}. */\n"
        "#include \"pie/core/lower/lower.h\"\n\n"
        f"{lower_body}"
    )
    for source_name in ("borrow.c",):
        symbol = f"{prefix}_{source_name[:-2]}"
        (feature_dir / source_name).write_text(
            f"/* Feature capsule unit for {manifest['id']}: {source_name}. */\n"
            "#include <stddef.h>\n\n"
            f"int {symbol}(void) {{\n"
            "    return 1;\n"
            "}\n"
        )
    (feature_dir / "tests").mkdir()
    (feature_dir / "tests" / "parse.pie").write_text(f"# Parse fixture for {manifest['id']}.\n")
    (feature_dir / "tests" / "negative.pie").write_text(f"# Negative fixture for {manifest['id']}.\n")
    print(f"created feature capsule {feature_dir}")


if __name__ == "__main__":
    main()
