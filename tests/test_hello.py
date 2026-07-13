"""Smoke test for the bundled hello example extension."""

from nanobind_namedtuple_examples import nbnt_example_hello


def test_hello_returns_greeting():
    assert nbnt_example_hello.hello() == "hello from nanobind_namedtuple"


def test_add_returns_sum():
    assert nbnt_example_hello.add(2, 3) == 5
