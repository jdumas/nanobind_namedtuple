"""Command-line entry point: write a pattern file for registered namedtuples.

Typical use, followed by a vanilla nanobind stubgen run consuming the file:

    python -m nanobind_namedtuple_stubgen -m my_ext -o my_ext.pat
    python -m nanobind.stubgen -m my_ext -p my_ext.pat -o my_ext.pyi
"""

from __future__ import annotations

import argparse
import importlib
import pkgutil
import sys

from . import generate_pattern_file


def parse_options(args: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m nanobind_namedtuple_stubgen",
        description="Generate a nanobind stubgen pattern file whose entries replace "
        "the stubs of namedtuple classes registered through nbnt::bind_namedtuple "
        "with canonical typing.NamedTuple blocks.",
    )

    parser.add_argument(
        "-o",
        "--output-file",
        metavar="FILE",
        dest="output_file",
        required=True,
        help="write the generated pattern file to the specified file",
    )

    parser.add_argument(
        "-m",
        "--module",
        action="append",
        metavar="MODULE",
        dest="modules",
        default=[],
        help="scan the specified module (can specify multiple times)",
    )

    parser.add_argument(
        "-i",
        "--import",
        action="append",
        metavar="PATH",
        dest="imports",
        default=[],
        help="add the directory to the Python import path (can specify multiple times)",
    )

    parser.add_argument(
        "-r",
        "--recursive",
        default=False,
        action="store_true",
        help="recursively scan submodules",
    )

    opt = parser.parse_args(args)
    if not opt.modules:
        parser.error("At least one module must be specified.")
    return opt


def main(args: list[str] | None = None) -> None:
    opt = parse_options(args)

    for path in opt.imports:
        sys.path.insert(0, path)

    modules = []
    seen = set()

    def add(name: str) -> None:
        if name in seen:
            return
        seen.add(name)
        module = importlib.import_module(name)
        modules.append(module)
        if opt.recursive and hasattr(module, "__path__"):
            for info in pkgutil.iter_modules(module.__path__, module.__name__ + "."):
                add(info.name)

    for name in opt.modules:
        add(name)

    with open(opt.output_file, "w", encoding="utf-8") as f:
        f.write(generate_pattern_file(modules))


if __name__ == "__main__":
    main()
