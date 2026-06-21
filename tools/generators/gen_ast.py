#!/usr/bin/env python3
import argparse
import json
import pathlib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default="src/features")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    nodes = []
    for path in sorted(pathlib.Path(args.root).rglob("feature.json")):
        data = json.loads(path.read_text())
        nodes.extend(data.get("ast_nodes", []))

    with pathlib.Path(args.out).open("w") as out:
        out.write("/* Auto-generated AST node ids. Do not edit. */\n")
        out.write("#ifndef PIE_GENERATED_AST_H\n#define PIE_GENERATED_AST_H\n\n")
        out.write("typedef enum PieGeneratedAstNode {\n")
        out.write("    PIE_AST_GENERATED_INVALID = 0,\n")
        for node in sorted(set(nodes)):
            out.write(f"    {node},\n")
        out.write("} PieGeneratedAstNode;\n\n#endif\n")


if __name__ == "__main__":
    main()
