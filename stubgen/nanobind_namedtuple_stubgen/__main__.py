"""Command-line entry point mirroring ``python -m nanobind.stubgen``.

Delegates to :func:`nanobind.stubgen.main` with :class:`NamedTupleStubGen` swapped
in place of the default ``StubGen`` class, so registered ``bind_namedtuple``
types render as canonical ``typing.NamedTuple`` blocks.
"""

from __future__ import annotations

import sys

from nanobind import stubgen as _nb_stubgen

from . import NamedTupleStubGen


def main() -> None:
    original = _nb_stubgen.StubGen
    _nb_stubgen.StubGen = NamedTupleStubGen  # type: ignore[misc]
    try:
        _nb_stubgen.main()
    finally:
        _nb_stubgen.StubGen = original  # type: ignore[misc]


if __name__ == "__main__":
    main()
    sys.exit(0)
