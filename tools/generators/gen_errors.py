#!/usr/bin/env python3
import argparse
import pathlib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    pathlib.Path(args.out).write_text(
        "/* Auto-generated diagnostic ids. Do not edit. */\n"
        "#ifndef PIE_GENERATED_ERRORS_H\n#define PIE_GENERATED_ERRORS_H\n\n"
        "typedef enum PieErrorCode {\n"
        "    PIE_E_OK = 0,\n"
        "    PIE_E_PARSE = 1000,\n"
        "    PIE_E_TYPE = 2000,\n"
        "    PIE_E_BORROW = 3000,\n"
        "    PIE_E_CODEGEN = 4000,\n"
        "} PieErrorCode;\n\n"
        "#endif\n"
    )


if __name__ == "__main__":
    main()
