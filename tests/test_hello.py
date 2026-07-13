"""Smoke test for the bundled hello example extension."""

import pytest
from nanobind_namedtuple_examples import nbnt_example_hello


def test_hello_returns_greeting():
    assert nbnt_example_hello.hello() == "hello from nanobind_namedtuple"


def test_add_returns_sum():
    assert nbnt_example_hello.add(2, 3) == 5


def test_color_from_cpp_is_plain_tuple():
    value = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    assert type(value) is tuple
    assert value == (1.0, 2.0, 3.0)
    assert len(value) == 3


def test_color_round_trip_from_python_tuple():
    assert nbnt_example_hello.sum_color((1.0, 2.0, 4.0)) == pytest.approx(7.0)


def test_point_round_trip_with_string_field():
    value = nbnt_example_hello.make_point(4, 5, "hello")
    assert type(value) is tuple
    assert value == (4, 5, "hello")
    assert nbnt_example_hello.point_label((7, 8, "world")) == "world"


def test_empty_record_round_trip():
    value = nbnt_example_hello.make_empty()
    assert type(value) is tuple
    assert value == ()
    assert nbnt_example_hello.take_empty(()) is True


@pytest.mark.parametrize(
    "bad_input",
    [
        (1.0, 2.0),
        (1.0, 2.0, 3.0, 4.0),
        (),
    ],
)
def test_color_rejects_wrong_arity(bad_input):
    with pytest.raises(TypeError):
        nbnt_example_hello.sum_color(bad_input)


def test_color_rejects_non_tuple_sequences():
    for bad in [[1.0, 2.0, 3.0], "abc", 1.0, {"r": 1.0, "g": 2.0, "b": 3.0}]:
        with pytest.raises(TypeError):
            nbnt_example_hello.sum_color(bad)


def test_color_rejects_tuple_subclass():
    class TupleSub(tuple):
        pass

    with pytest.raises(TypeError):
        nbnt_example_hello.sum_color(TupleSub((1.0, 2.0, 3.0)))


def test_color_rejects_wrong_element_type():
    with pytest.raises(TypeError):
        nbnt_example_hello.sum_color((1.0, "two", 3.0))


def test_empty_rejects_wrong_arity():
    with pytest.raises(TypeError):
        nbnt_example_hello.take_empty((1,))
