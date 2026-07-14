"""Smoke test for the bundled hello example extension."""

import pytest
from nanobind_namedtuple_examples import nbnt_example_hello


def test_hello_returns_greeting():
    assert nbnt_example_hello.hello() == "hello from nanobind_namedtuple"


def test_add_returns_sum():
    assert nbnt_example_hello.add(2, 3) == 5


def test_color_from_cpp_is_registered_class():
    value = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    assert type(value) is nbnt_example_hello.Color
    assert isinstance(value, tuple)
    assert value == (1.0, 2.0, 3.0)
    assert len(value) == 3
    assert value.r == 1.0
    assert value.g == 2.0
    assert value.b == 3.0
    assert nbnt_example_hello.Color._fields == ("r", "g", "b")


def test_color_round_trip_from_python_tuple():
    assert nbnt_example_hello.sum_color((1.0, 2.0, 4.0)) == pytest.approx(7.0)


def test_point_round_trip_with_string_field():
    value = nbnt_example_hello.make_point(4, 5, "hello")
    assert type(value) is nbnt_example_hello.Point
    assert value == (4, 5, "hello")
    assert value.label == "hello"
    assert nbnt_example_hello.point_label((7, 8, "world")) == "world"


def test_point_has_trailing_default():
    assert nbnt_example_hello.Point._field_defaults == {"label": ""}
    made = nbnt_example_hello.Point(1, 2)
    assert made == (1, 2, "")


def test_empty_record_round_trip():
    value = nbnt_example_hello.make_empty()
    assert type(value) is nbnt_example_hello.Empty
    assert value == ()
    assert nbnt_example_hello.Empty._fields == ()
    assert nbnt_example_hello.take_empty(()) is True


def test_registered_class_module_is_example():
    assert nbnt_example_hello.Color.__module__ == nbnt_example_hello.__name__


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


def test_color_accepts_registered_class_instance():
    instance = nbnt_example_hello.Color(1.0, 2.0, 4.0)
    assert nbnt_example_hello.sum_color(instance) == pytest.approx(7.0)


def test_point_accepts_registered_class_instance():
    instance = nbnt_example_hello.Point(4, 5, "hello")
    assert nbnt_example_hello.point_label(instance) == "hello"


def test_empty_accepts_registered_class_instance():
    assert nbnt_example_hello.take_empty(nbnt_example_hello.Empty()) is True


def test_color_rejects_other_registered_namedtuple():
    other = nbnt_example_hello.Vec3(1.0, 2.0, 3.0)
    with pytest.raises(TypeError):
        nbnt_example_hello.sum_color(other)


def test_vec3_rejects_color_instance():
    color = nbnt_example_hello.Color(1.0, 2.0, 3.0)
    with pytest.raises(TypeError):
        nbnt_example_hello.sum_vec3(color)


def test_color_rejects_stdlib_namedtuple_of_same_arity():
    from collections import namedtuple

    Other = namedtuple("Other", ["r", "g", "b"])
    with pytest.raises(TypeError):
        nbnt_example_hello.sum_color(Other(1.0, 2.0, 3.0))


def test_repeated_registration_preserves_class_identity():
    original = nbnt_example_hello.Color
    original_id = id(original)
    instance_before = nbnt_example_hello.make_color(0.25, 0.5, 0.75)
    assert isinstance(instance_before, original)

    nbnt_example_hello.rebind_color(nbnt_example_hello)

    assert nbnt_example_hello.Color is original
    assert id(nbnt_example_hello.Color) == original_id
    assert isinstance(instance_before, nbnt_example_hello.Color)
    instance_after = nbnt_example_hello.make_color(0.1, 0.2, 0.3)
    assert type(instance_after) is nbnt_example_hello.Color
