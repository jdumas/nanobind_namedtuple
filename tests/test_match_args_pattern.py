"""Structural pattern matching over bound namedtuple classes.

Isolated into its own file so ``tests/conftest.py`` can exclude it from
collection on Python 3.9, where ``match``/``case`` is a parse-time syntax
error. Real pattern-matching coverage still runs on 3.10+.
"""

from __future__ import annotations

import pytest
from nanobind_namedtuple_examples import nbnt_example_hello


def test_match_args_enables_structural_pattern():
    value = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    match value:
        case nbnt_example_hello.Color(r, g, b):
            assert (r, g, b) == (1.0, 2.0, 3.0)
        case _:  # pragma: no cover - defensive
            pytest.fail("structural match failed")
