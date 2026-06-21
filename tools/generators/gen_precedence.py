#!/usr/bin/env python3
import argparse
import json
import pathlib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default="src/features")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    operators = []
    for path in sorted(pathlib.Path(args.root).rglob("feature.json")):
        data = json.loads(path.read_text())
        for op in data.get("operators", []):
            operators.append(op)

    with pathlib.Path(args.out).open("w") as out:
        out.write("/* Auto-generated precedence placeholder. Do not edit. */\n")
        out.write("#ifndef PIE_GENERATED_PRECEDENCE_H\n#define PIE_GENERATED_PRECEDENCE_H\n\n")
        out.write("typedef struct PieGeneratedOperator { const char *text; int precedence; } PieGeneratedOperator;\n")
        out.write("static const PieGeneratedOperator pie_generated_operators[] = {\n")
        for op in sorted(set(operators)):
            out.write(f"    {{\"{op}\", 0}},\n")
        out.write("};\n\n#endif\n")


if __name__ == "__main__":
    main()
