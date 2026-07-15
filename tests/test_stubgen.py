"""Verify the ``__nb_named_tuple__`` sentinel and the ``NamedTupleStubGen`` hook.

Covers:

* the sentinel attribute and per-field annotation metadata attached by
  ``nbnt::bind_namedtuple<T>``;
* ``NamedTupleStubGen`` overriding nanobind's default ``class X(tuple): ...``
  emission with canonical ``class X(typing.NamedTuple):`` blocks that include
  annotations and trailing defaults;
* class and per-field docstrings set at bind time surfacing in the stub while
  synthetic ``collections.namedtuple`` docstrings stay suppressed;
* the fallback to nanobind's default emitter for classes without the sentinel.
"""

from __future__ import annotations

import re
import sys
import typing

import pytest
from nanobind.stubgen import StubGen
from nanobind_namedtuple_examples import nbnt_example_hello
from nanobind_namedtuple_stubgen import NamedTupleStubGen, is_namedtuple_class

NT_CLASSES = ("Color", "Point", "Empty", "Vec3", "Payload", "Pixel", "Tagged", "Polyline")


def _optional(inner: str) -> str:
    """Optional annotation as nanobind renders it for the running interpreter."""
    if sys.version_info >= (3, 10):
        return f"{inner} | None"
    return f"typing.Optional[{inner}]"


@pytest.mark.parametrize("cls_name", NT_CLASSES)
def test_sentinel_attached_to_registered_classes(cls_name):
    cls = getattr(nbnt_example_hello, cls_name)
    assert cls.__nb_named_tuple__ is True
    assert is_namedtuple_class(cls)


def test_annotation_metadata_matches_declared_fields():
    color = nbnt_example_hello.Color
    assert list(color.__nb_nt_annotations__.keys()) == list(color._fields)
    assert color.__nb_nt_annotations__ == {"r": "float", "g": "float", "b": "float"}


def test_annotation_metadata_covers_string_and_object_fields():
    assert nbnt_example_hello.Point.__nb_nt_annotations__ == {
        "x": "int",
        "y": "int",
        "label": "str",
    }
    assert nbnt_example_hello.Payload.__nb_nt_annotations__ == {"obj": "object"}
    assert nbnt_example_hello.Empty.__nb_nt_annotations__ == {}


def test_annotations_attribute_populated_with_string_types():
    assert nbnt_example_hello.Color.__annotations__ == {
        "r": "float",
        "g": "float",
        "b": "float",
    }


@pytest.mark.parametrize("cls_name", NT_CLASSES)
def test_annotations_carry_no_stubgen_markup(cls_name):
    cls = getattr(nbnt_example_hello, cls_name)
    for mapping in (cls.__annotations__, cls.__nb_nt_annotations__):
        for fname, annot in mapping.items():
            assert "@" not in annot, f"{cls_name}.{fname}: {annot!r}"


def test_stl_caster_annotations_are_clean_type_expressions():
    assert nbnt_example_hello.Polyline.__annotations__ == {
        "points": "list[tuple[int, int]]",
        "width": _optional("float"),
    }
    assert nbnt_example_hello.Tagged.__annotations__ == {
        "value": "int",
        "tag": _optional("int"),
    }


def test_get_type_hints_succeeds_on_stl_caster_fields():
    hints = typing.get_type_hints(nbnt_example_hello.Polyline)
    assert hints["points"] == list[tuple[int, int]]
    assert hints["width"] == typing.Optional[float]


def test_annotations_and_sentinel_are_distinct_dict_objects():
    color = nbnt_example_hello.Color
    assert color.__annotations__ == color.__nb_nt_annotations__
    assert color.__annotations__ is not color.__nb_nt_annotations__
    sentinel_snapshot = dict(color.__nb_nt_annotations__)
    try:
        color.__annotations__["r"] = "int"
        color.__annotations__["injected"] = "bogus"
        assert color.__nb_nt_annotations__ == sentinel_snapshot
    finally:
        color.__annotations__.pop("injected", None)
        color.__annotations__["r"] = "float"


def _generate_stub(cls, *, hook, include_docstrings=False):
    gen = hook(module=nbnt_example_hello, include_docstrings=include_docstrings)
    gen.put(cls, name=cls.__name__, parent=nbnt_example_hello)
    return gen.get()


def test_hook_emits_typing_namedtuple_block_for_color():
    stub = _generate_stub(nbnt_example_hello.Color, hook=NamedTupleStubGen)
    assert re.search(r"class Color\((?:typing\.)?NamedTuple\):", stub)
    assert "r: float" in stub
    assert "g: float" in stub
    assert "b: float" in stub


def test_hook_emits_defaults_from_field_defaults():
    stub = _generate_stub(nbnt_example_hello.Point, hook=NamedTupleStubGen)
    assert re.search(r"class Point\((?:typing\.)?NamedTuple\):", stub)
    assert "x: int" in stub
    assert "y: int" in stub
    assert re.search(r"label: str = ['\"]{2}", stub)


def test_hook_emits_clean_annotations_for_stl_caster_fields():
    stub = _generate_stub(nbnt_example_hello.Polyline, hook=NamedTupleStubGen)
    assert re.search(r"class Polyline\((?:typing\.)?NamedTuple\):", stub)
    assert "points: list[tuple[int, int]]" in stub
    if sys.version_info >= (3, 10):
        assert "width: float | None" in stub
    else:
        assert "width: Optional[float]" in stub
    assert "@" not in stub
    # Import lines must name plain symbols, never subscripted expressions.
    import_lines = [ln for ln in stub.splitlines() if ln.startswith(("import ", "from "))]
    assert all("[" not in ln for ln in import_lines), import_lines


def test_hook_emits_class_docstring_for_documented_class():
    stub = _generate_stub(nbnt_example_hello.Point, hook=NamedTupleStubGen, include_docstrings=True)
    assert '"""A 2D point with an optional display label."""' in stub


def test_hook_emits_field_docstring_under_documented_field():
    stub = _generate_stub(nbnt_example_hello.Point, hook=NamedTupleStubGen, include_docstrings=True)
    assert re.search(r'x: int\n\s+"""X coordinate\."""', stub)
    assert re.search(r"y: int\n\s+label:", stub)


def test_hook_emits_field_docstring_after_default_value():
    stub = _generate_stub(nbnt_example_hello.Point, hook=NamedTupleStubGen, include_docstrings=True)
    assert re.search(r"label: str = ['\"]{2}\n\s+\"\"\"Display label\.\"\"\"", stub)

    tagged = _generate_stub(
        nbnt_example_hello.Tagged, hook=NamedTupleStubGen, include_docstrings=True
    )
    assert re.search(r'tag: .+ = None\n\s+"""Optional integer tag\."""', tagged)


def test_hook_emits_no_synthetic_docstrings_for_undocumented_class():
    with_docs = _generate_stub(
        nbnt_example_hello.Color, hook=NamedTupleStubGen, include_docstrings=True
    )
    without_docs = _generate_stub(nbnt_example_hello.Color, hook=NamedTupleStubGen)
    assert (
        with_docs[with_docs.index("class Color") :]
        == without_docs[without_docs.index("class Color") :]
    )
    assert "Color(r, g, b)" not in with_docs
    assert "Alias for field number" not in with_docs


def test_hook_omits_docstrings_when_disabled():
    stub = _generate_stub(nbnt_example_hello.Point, hook=NamedTupleStubGen)
    assert '"""' not in stub


def test_hook_emits_pass_for_zero_field_record():
    stub = _generate_stub(nbnt_example_hello.Empty, hook=NamedTupleStubGen)
    assert re.search(r"class Empty\((?:typing\.)?NamedTuple\):", stub)
    assert re.search(r"class Empty\(.*\):\s+pass", stub)


def test_hook_overrides_default_tuple_emission():
    default_stub = _generate_stub(nbnt_example_hello.Color, hook=StubGen)
    assert "class Color(tuple):" in default_stub
    assert "NamedTuple" not in default_stub

    hooked_stub = _generate_stub(nbnt_example_hello.Color, hook=NamedTupleStubGen)
    assert "class Color(tuple):" not in hooked_stub
    assert "NamedTuple" in hooked_stub


def test_hook_leaves_non_namedtuple_classes_to_default_emitter():
    class Plain:
        pass

    plain_stub_default = _generate_stub(Plain, hook=StubGen)
    plain_stub_hook = _generate_stub(Plain, hook=NamedTupleStubGen)
    assert plain_stub_default == plain_stub_hook


def test_full_module_stub_contains_every_namedtuple_class():
    gen = NamedTupleStubGen(module=nbnt_example_hello, include_docstrings=False)
    gen.put(nbnt_example_hello)
    stub = gen.get()
    for cls_name in NT_CLASSES:
        assert re.search(
            rf"class {cls_name}\((?:typing\.)?NamedTuple\):", stub
        ), f"missing typing.NamedTuple block for {cls_name}"


def test_is_namedtuple_class_rejects_unrelated_types():
    assert not is_namedtuple_class(int)
    assert not is_namedtuple_class(tuple)
    assert not is_namedtuple_class(nbnt_example_hello.hello)

    from collections import namedtuple

    Plain = namedtuple("Plain", ["x", "y"])
    assert not is_namedtuple_class(Plain)
