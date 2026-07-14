"""Verify the ``collections.namedtuple`` surface inherited by bound classes.

Covers ``_fields``, ``_field_defaults``, ``_make``, ``_replace``,
``_asdict``, ``__getnewargs__``, ``__repr__``, read-only field descriptors,
strict immutability, tuple behaviour, and reference-cycle collection
through an ``nb::object``-typed field. ``__match_args__`` coverage lives
in ``tests/test_match_args_pattern.py``, which is gated to Python 3.10+
(matching the stdlib, which only defines ``__match_args__`` there).
"""

from __future__ import annotations

import gc
import sys
import weakref

import pytest
from nanobind_namedtuple_examples import nbnt_example_hello


def test_fields_are_declared_names():
    assert nbnt_example_hello.Color._fields == ("r", "g", "b")
    assert nbnt_example_hello.Point._fields == ("x", "y", "label")
    assert nbnt_example_hello.Empty._fields == ()


def test_field_defaults_reflects_trailing_defaults():
    assert nbnt_example_hello.Color._field_defaults == {}
    assert nbnt_example_hello.Point._field_defaults == {"label": ""}
    assert nbnt_example_hello.Empty._field_defaults == {}


def test_make_builds_from_iterable():
    value = nbnt_example_hello.Color._make(iter([0.25, 0.5, 0.75]))
    assert type(value) is nbnt_example_hello.Color
    assert value == (0.25, 0.5, 0.75)


def test_replace_returns_new_instance_with_field_updated():
    original = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    updated = original._replace(g=99.0)
    assert type(updated) is nbnt_example_hello.Color
    assert updated == (1.0, 99.0, 3.0)
    assert original == (1.0, 2.0, 3.0)
    assert updated is not original


def test_asdict_returns_field_mapping():
    value = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    assert value._asdict() == {"r": 1.0, "g": 2.0, "b": 3.0}
    assert nbnt_example_hello.make_empty()._asdict() == {}


def test_getnewargs_returns_positional_tuple():
    value = nbnt_example_hello.make_point(7, 8, "world")
    assert value.__getnewargs__() == (7, 8, "world")


def test_repr_uses_class_and_field_names():
    value = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    assert repr(value) == "Color(r=1.0, g=2.0, b=3.0)"
    assert repr(nbnt_example_hello.make_empty()) == "Empty()"


def test_field_descriptors_expose_named_access():
    value = nbnt_example_hello.make_point(4, 5, "hi")
    assert value.x == 4
    assert value.y == 5
    assert value.label == "hi"
    descriptor = type(value).__dict__["x"]
    assert hasattr(descriptor, "__get__")
    assert descriptor.__get__(value, type(value)) == 4


def test_setting_field_raises_attribute_error():
    value = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    with pytest.raises(AttributeError):
        value.r = 99.0  # type: ignore[misc]


def test_deleting_field_raises_attribute_error():
    value = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    with pytest.raises(AttributeError):
        del value.r  # type: ignore[misc]


def test_assigning_unknown_attribute_raises_attribute_error():
    value = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    with pytest.raises(AttributeError):
        value.extra = 1  # type: ignore[misc]


def test_instance_has_no_dict_slot():
    value = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    assert not hasattr(value, "__dict__")


def test_indexing_iteration_unpacking_and_len_behave_like_tuple():
    value = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    assert value[0] == 1.0
    assert value[-1] == 3.0
    assert list(value) == [1.0, 2.0, 3.0]
    r, g, b = value
    assert (r, g, b) == (1.0, 2.0, 3.0)
    assert len(value) == 3
    assert isinstance(value, tuple)


def test_equality_with_plain_tuple_and_other_namedtuple():
    value = nbnt_example_hello.make_color(1.0, 2.0, 3.0)
    assert value == (1.0, 2.0, 3.0)
    other = nbnt_example_hello.Vec3(1.0, 2.0, 3.0)
    assert value == other  # tuple equality is structural
    assert hash(value) == hash((1.0, 2.0, 3.0))


def test_payload_round_trip_preserves_object_identity():
    marker = object()
    value = nbnt_example_hello.make_payload(marker)
    assert type(value) is nbnt_example_hello.Payload
    assert value.obj is marker
    assert nbnt_example_hello.payload_obj(value) is marker


@pytest.mark.skipif(
    sys.implementation.name == "pypy",
    reason="PyPy cpyext does not collect cycles through C-extension objects",
)
def test_reference_cycle_through_object_field_is_collected():
    class Holder:
        pass

    holder = Holder()
    holder.payload = nbnt_example_hello.make_payload(holder)
    weak = weakref.ref(holder)

    del holder
    for _ in range(6):
        if weak() is None:
            break
        gc.collect()

    assert weak() is None
