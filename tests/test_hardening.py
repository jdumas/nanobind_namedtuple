"""Cover nested-record, optional-field, rv_policy, and pickle behaviour."""

from __future__ import annotations

import pickle

import pytest
from nanobind_namedtuple_examples import nbnt_example_hello


def test_nested_record_from_cpp_produces_registered_class():
    pixel = nbnt_example_hello.make_pixel(
        nbnt_example_hello.Vec3(1.0, 2.0, 3.0),
        nbnt_example_hello.Color(0.25, 0.5, 0.75),
    )
    assert type(pixel) is nbnt_example_hello.Pixel
    assert type(pixel.position) is nbnt_example_hello.Vec3
    assert type(pixel.color) is nbnt_example_hello.Color
    assert pixel.position == (1.0, 2.0, 3.0)
    assert pixel.color == (0.25, 0.5, 0.75)


def test_nested_record_accepts_registered_and_plain_tuples():
    color = nbnt_example_hello.pixel_color(
        (nbnt_example_hello.Vec3(1.0, 2.0, 3.0), nbnt_example_hello.Color(0.25, 0.5, 0.75))
    )
    assert type(color) is nbnt_example_hello.Color
    assert color == (0.25, 0.5, 0.75)

    position = nbnt_example_hello.pixel_position(((1.0, 2.0, 3.0), (0.25, 0.5, 0.75)))
    assert type(position) is nbnt_example_hello.Vec3
    assert position == (1.0, 2.0, 3.0)


def test_nested_record_rejects_wrong_shape():
    with pytest.raises(TypeError):
        nbnt_example_hello.pixel_color(((1.0, 2.0), (0.25, 0.5, 0.75)))
    with pytest.raises(TypeError):
        nbnt_example_hello.pixel_color(((1.0, 2.0, 3.0), (0.25, 0.5)))


def test_optional_field_engaged_round_trip():
    tagged = nbnt_example_hello.make_tagged(7, 42)
    assert type(tagged) is nbnt_example_hello.Tagged
    assert tagged.value == 7
    assert tagged.tag == 42
    assert nbnt_example_hello.tagged_tag(tagged) == 42
    assert nbnt_example_hello.tagged_tag((7, 42)) == 42


def test_optional_field_disengaged_round_trip():
    tagged = nbnt_example_hello.make_tagged(7, None)
    assert tagged.value == 7
    assert tagged.tag is None
    assert nbnt_example_hello.tagged_tag(tagged) is None
    assert nbnt_example_hello.tagged_tag((7, None)) is None


def test_optional_field_default_is_none():
    assert nbnt_example_hello.Tagged._field_defaults == {"tag": None}
    made = nbnt_example_hello.Tagged(7)
    assert made == (7, None)


def test_rv_policy_reference_downgrades_to_by_value():
    value = nbnt_example_hello.make_color_reference(1.0, 2.0, 3.0)
    assert type(value) is nbnt_example_hello.Color
    assert value == (1.0, 2.0, 3.0)


def test_rv_policy_copy_and_move_pass_through():
    for factory in (
        nbnt_example_hello.make_color_copy,
        nbnt_example_hello.make_color_move,
    ):
        value = factory(1.0, 2.0, 3.0)
        assert type(value) is nbnt_example_hello.Color
        assert value == (1.0, 2.0, 3.0)


def test_rv_policy_reference_internal_is_rejected():
    with pytest.raises(TypeError, match="reference_internal|take_ownership"):
        nbnt_example_hello.make_color_reference_internal(1.0, 2.0, 3.0)


def test_rv_policy_take_ownership_is_rejected():
    with pytest.raises(TypeError, match="reference_internal|take_ownership"):
        nbnt_example_hello.make_color_take_ownership(1.0, 2.0, 3.0)


@pytest.mark.parametrize(
    "value",
    [
        nbnt_example_hello.Color(1.0, 2.0, 3.0),
        nbnt_example_hello.Point(4, 5, "hi"),
        nbnt_example_hello.Empty(),
        nbnt_example_hello.Vec3(7.0, 8.0, 9.0),
        nbnt_example_hello.Pixel(
            nbnt_example_hello.Vec3(1.0, 2.0, 3.0),
            nbnt_example_hello.Color(0.25, 0.5, 0.75),
        ),
        nbnt_example_hello.Tagged(7, 42),
        nbnt_example_hello.Tagged(7, None),
    ],
)
def test_pickle_round_trip_within_module(value):
    for protocol in range(pickle.HIGHEST_PROTOCOL + 1):
        restored = pickle.loads(pickle.dumps(value, protocol=protocol))
        assert type(restored) is type(value)
        assert restored == value
