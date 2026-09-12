#!/usr/bin/env python3
"""Pack a source tree into a DCC1/DCP1 Dynphony persistent image."""

import argparse
from pathlib import Path

from dynphony.project import make_persistent_image, project_from_directory


def number(value):
    return int(value, 0)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument("--persistent-size", type=number, default=1 << 24)
    parser.add_argument("--load-address", type=number, default=8192)
    parser.add_argument("-I", dest="include_roots", action="append")
    parser.add_argument("-D", dest="definitions", action="append", default=[])
    parser.add_argument(
        "--exclude", action="append", default=[], metavar="RELATIVE_PATH",
        help="exclude a file or directory from the virtual project",
    )
    args = parser.parse_args(argv)
    project = project_from_directory(
        args.directory, include_roots=args.include_roots,
        definitions=args.definitions,
        exclude=args.exclude,
    )
    image = make_persistent_image(
        project, persistent_size=args.persistent_size,
        program_load_address=args.load_address,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(
        f"Packed {len(project.files)} files into {args.output} "
        f"({len(image)} bytes)"
    )


if __name__ == "__main__":
    main()
