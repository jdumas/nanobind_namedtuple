"""``__match_args__`` and structural pattern matching over bound classes.

Isolated into its own file so ``tests/conftest.py`` can exclude it from
collection on Python 3.9, where ``match``/``case`` is a parse-time syntax
error and ``collections.namedtuple`` does not synthesise
``__match_args__`` either. Both live here so they gate together; 3.10+
still exercises real pattern-matching coverage.
"""

from __future__ import annotations

import pytest
from nanobind_namedtuple_examples import nbnt_example_hello


def test_match_args_matches_fields():
    assert nbnt_example_hello.Color.__match_args__ == nbnt_example_hello.Color._fields
    assert nbnt_example_hello.Point.__match_args__ == nbnt_example_hello.Point._fields
    assert nbnt_example_hello.Empty.__match_args__ == nbnt_example_hello.Empty._fields


def test_match_args_enables_structural_pattern():
    value = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    match value:
        case nbnt_example_hello.Color(r, g, b):
            assert (r, g, b) == (1.0, 2.0, 3.0)
        case _:  # pragma: no cover - defensive
            pytest.fail("structural match failed")
