#!/usr/bin/env python3
import argparse
import json
import pathlib
import sys

REQUIRED = {
    "id",
    "name",
    "group",
    "version",
    "kind",
    "deps",
    "tokens",
    "keywords",
    "operators",
    "ast_nodes",
    "type_kinds",
    "hooks",
}


def load_manifests(root: pathlib.Path):
    manifests = []
    for path in sorted(root.rglob("feature.json")):
        try:
            data = json.loads(path.read_text())
        except json.JSONDecodeError as exc:
            raise SystemExit(f"{path}: invalid JSON: {exc}") from exc
        missing = sorted(REQUIRED - set(data))
        if missing:
            raise SystemExit(f"{path}: missing required field(s): {', '.join(missing)}")
        data["_path"] = str(path)
        manifests.append(data)
    return manifests


def ensure_list(feature, field):
    value = feature[field]
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise SystemExit(f"{feature['_path']}: {field} must be a list of strings")


def check_unique(manifests, field, item_label):
    seen = {}
    for feature in manifests:
        for item in feature[field]:
            if item in seen:
                raise SystemExit(
                    f"{feature['_path']}: duplicate {item_label} '{item}' already declared by {seen[item]}"
                )
            seen[item] = feature["id"]


def check_dependency_graph(manifests):
    ids = {feature["id"] for feature in manifests}
    for feature in manifests:
        for dep in feature["deps"]:
            if dep not in ids:
                raise SystemExit(f"{feature['_path']}: dependency '{dep}' does not exist")

    visiting = set()
    visited = set()
    by_id = {feature["id"]: feature for feature in manifests}

    def visit(feature_id):
        if feature_id in visiting:
            raise SystemExit(f"feature dependency cycle includes '{feature_id}'")
        if feature_id in visited:
            return
        visiting.add(feature_id)
        for dep in by_id[feature_id]["deps"]:
            visit(dep)
        visiting.remove(feature_id)
        visited.add(feature_id)

    for feature in manifests:
        visit(feature["id"])


def check_parse_hooks(feature):
    hooks = feature.get("parse_hooks")
    if hooks is None:
        return
    if not isinstance(hooks, dict):
        raise SystemExit(f"{feature['_path']}: parse_hooks must be an object")
    for field in ("top_level", "stmt", "expr_prefix", "expr_infix"):
        value = hooks.get(field, [])
        if not isinstance(value, list) or not all(isinstance(item, str) and item for item in value):
            raise SystemExit(f"{feature['_path']}: parse_hooks.{field} must be a list of strings")


def check_asm_hooks(feature):
    hooks = feature.get("asm_hooks")
    if hooks is None:
        return
    if not isinstance(hooks, dict):
        raise SystemExit(f"{feature['_path']}: asm_hooks must be an object")
    for field in ("stmt", "expr"):
        value = hooks.get(field, [])
        if not isinstance(value, list) or not all(isinstance(item, str) and item for item in value):
            raise SystemExit(f"{feature['_path']}: asm_hooks.{field} must be a list of strings")


def check_sema_hooks(feature):
    hooks = feature.get("sema_hooks")
    if hooks is None:
        return
    if not isinstance(hooks, dict):
        raise SystemExit(f"{feature['_path']}: sema_hooks must be an object")
    for field in ("stmt", "expr"):
        value = hooks.get(field, [])
        if not isinstance(value, list) or not all(isinstance(item, str) and item for item in value):
            raise SystemExit(f"{feature['_path']}: sema_hooks.{field} must be a list of strings")


def check_lower_hooks(feature):
    hooks = feature.get("lower_hooks")
    if hooks is None:
        return
    if not isinstance(hooks, dict):
        raise SystemExit(f"{feature['_path']}: lower_hooks must be an object")
    for field in ("stmt", "expr"):
        value = hooks.get(field, [])
        if not isinstance(value, list) or not all(isinstance(item, str) and item for item in value):
            raise SystemExit(f"{feature['_path']}: lower_hooks.{field} must be a list of strings")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    args = parser.parse_args()

    root = pathlib.Path(args.root)
    manifests = load_manifests(root)
    ids = {}
    for feature in manifests:
        for field in REQUIRED:
            if field == "_path":
                continue
        for string_field in ("id", "name", "group", "version", "kind"):
            if not isinstance(feature[string_field], str) or not feature[string_field]:
                raise SystemExit(f"{feature['_path']}: {string_field} must be a non-empty string")
        if feature["id"] in ids:
            raise SystemExit(f"{feature['_path']}: duplicate feature id '{feature['id']}'")
        ids[feature["id"]] = feature["_path"]
        for list_field in ("deps", "tokens", "keywords", "operators", "ast_nodes", "type_kinds", "hooks"):
            ensure_list(feature, list_field)
        check_parse_hooks(feature)
        check_sema_hooks(feature)
        check_lower_hooks(feature)
        check_asm_hooks(feature)

    check_unique(manifests, "tokens", "token")
    check_unique(manifests, "keywords", "keyword")
    check_unique(manifests, "operators", "operator")
    check_unique(manifests, "ast_nodes", "AST node")
    check_unique(manifests, "type_kinds", "type kind")
    check_dependency_graph(manifests)
    print(f"validated {len(manifests)} Pie feature manifests")


if __name__ == "__main__":
    main()
