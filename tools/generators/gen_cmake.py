#!/usr/bin/env python3
import argparse
import json
import pathlib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default="src/features")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    manifests = sorted(pathlib.Path(args.root).rglob("feature.json"))
    with pathlib.Path(args.out).open("w") as out:
        out.write("# Auto-generated feature manifest list. Do not edit.\n")
        out.write("set(PIE_KNOWN_FEATURE_MANIFESTS\n")
        for manifest in manifests:
            data = json.loads(manifest.read_text())
            out.write(f"    # {data['id']}\n")
            out.write(f"    {manifest.as_posix()}\n")
        out.write(")\n")


if __name__ == "__main__":
    main()
