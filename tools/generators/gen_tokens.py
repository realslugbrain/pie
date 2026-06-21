#!/usr/bin/env python3
import argparse
import json
import pathlib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default="src/features")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    root = pathlib.Path(args.root)
    tokens = []
    keywords = []
    for path in sorted(root.rglob("feature.json")):
        data = json.loads(path.read_text())
        tokens.extend(data.get("tokens", []))
        keywords.extend(data.get("keywords", []))

    with pathlib.Path(args.out).open("w") as out:
        out.write("/* Auto-generated token table. Do not edit. */\n")
        out.write("#ifndef PIE_GENERATED_TOKENS_H\n#define PIE_GENERATED_TOKENS_H\n\n")
        out.write("typedef enum PieTokenKind {\n")
        out.write("    PIE_TOK_EOF,\n    PIE_TOK_IDENTIFIER,\n    PIE_TOK_INTEGER,\n    PIE_TOK_STRING,\n")
        for token in sorted(set(tokens)):
            out.write(f"    {token},\n")
        out.write("} PieTokenKind;\n\n")
        out.write("static const char * const pie_keyword_table[] = {\n")
        for keyword in sorted(set(keywords)):
            out.write(f"    \"{keyword}\",\n")
        out.write("};\n\n#endif\n")


if __name__ == "__main__":
    main()
