"""Verify the ``__nb_named_tuple__`` sentinel and the pattern-file generator.

Covers:

* the sentinel attribute and per-field annotation metadata attached by
  ``nbnt::bind_namedtuple<T>``;
* pattern-file entries replacing nanobind's default ``class X(tuple): ...``
  emission with canonical ``class X(NamedTuple):`` blocks that include
  annotations and trailing defaults;
* class and per-field docstrings set at bind time surfacing in the replacement
  block while synthetic ``collections.namedtuple`` docstrings stay suppressed;
* an end-to-end run of vanilla ``python -m nanobind.stubgen`` consuming the
  generated pattern file.
"""

from __future__ import annotations

import inspect
import re
import subprocess
import sys
import typing

import pytest
from nanobind_namedtuple_examples import nbnt_example_hello
from nanobind_namedtuple_stubgen import (
    find_namedtuple_classes,
    generate_pattern_file,
    is_namedtuple_class,
    pattern_entry,
)

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


@pytest.mark.parametrize("cls_name", NT_CLASSES)
def test_new_annotations_mirror_class_annotations(cls_name):
    cls = getattr(nbnt_example_hello, cls_name)
    assert cls.__new__.__annotations__ == cls.__annotations__
    assert cls.__new__.__annotations__ is not cls.__annotations__
    assert cls.__new__.__annotations__ is not cls.__nb_nt_annotations__


def test_getfullargspec_exposes_constructor_annotations():
    spec = inspect.getfullargspec(nbnt_example_hello.Color)
    assert spec.annotations == {"r": "float", "g": "float", "b": "float"}


MODULE_NAME = "nanobind_namedtuple_examples.nbnt_example_hello"


def test_find_namedtuple_classes_returns_registered_classes_in_order():
    classes = find_namedtuple_classes(nbnt_example_hello)
    assert [cls.__name__ for cls in classes] == list(NT_CLASSES)


def test_find_namedtuple_classes_skips_aliases_and_foreign_modules():
    import types

    fake = types.ModuleType("fake_mod")
    fake.Renamed = nbnt_example_hello.Color  # alias under a different name
    fake.Color = nbnt_example_hello.Color  # right name, wrong __module__
    assert find_namedtuple_classes(fake) == []


def test_pattern_entry_query_is_anchored_escaped_qualified_name():
    entry = pattern_entry(nbnt_example_hello.Color)
    query = entry.splitlines()[0]
    assert query == rf"^{re.escape(MODULE_NAME)}\.Color$:"


def test_pattern_entry_emits_namedtuple_block_for_color():
    entry = pattern_entry(nbnt_example_hello.Color)
    assert "\\from typing import NamedTuple" in entry
    assert "class Color(NamedTuple):" in entry
    assert "r: float" in entry
    assert "g: float" in entry
    assert "b: float" in entry


def test_pattern_entry_emits_defaults_from_field_defaults():
    entry = pattern_entry(nbnt_example_hello.Point)
    assert "class Point(NamedTuple):" in entry
    assert "x: int" in entry
    assert "y: int" in entry
    assert re.search(r"label: str = ['\"]{2}", entry)


def test_pattern_entry_emits_clean_annotations_for_stl_caster_fields():
    entry = pattern_entry(nbnt_example_hello.Polyline)
    assert "class Polyline(NamedTuple):" in entry
    assert "points: list[tuple[int, int]]" in entry
    if sys.version_info >= (3, 10):
        assert "width: float | None" in entry
    else:
        # Annotation strings are emitted verbatim; the referenced module is
        # registered through an ``\import`` escape line.
        assert "width: typing.Optional[float]" in entry
        assert "\\import typing" in entry
    assert "@" not in entry


def test_pattern_entry_emits_class_docstring_for_documented_class():
    entry = pattern_entry(nbnt_example_hello.Point)
    assert '"""A 2D point with an optional display label."""' in entry


def test_pattern_entry_emits_field_docstring_under_documented_field():
    entry = pattern_entry(nbnt_example_hello.Point)
    assert re.search(r'x: int\n\s+"""X coordinate\."""', entry)
    assert re.search(r"y: int\n\s+label:", entry)


def test_pattern_entry_emits_field_docstring_after_default_value():
    entry = pattern_entry(nbnt_example_hello.Point)
    assert re.search(r"label: str = ['\"]{2}\n\s+\"\"\"Display label\.\"\"\"", entry)

    tagged = pattern_entry(nbnt_example_hello.Tagged)
    assert re.search(r'tag: .+ = None\n\s+"""Optional integer tag\."""', tagged)


def test_pattern_entry_emits_no_synthetic_docstrings_for_undocumented_class():
    entry = pattern_entry(nbnt_example_hello.Color)
    assert "Color(r, g, b)" not in entry
    assert "Alias for field number" not in entry
    assert '"""' not in entry


def test_pattern_entry_emits_pass_for_zero_field_record():
    entry = pattern_entry(nbnt_example_hello.Empty)
    assert "class Empty(NamedTuple):" in entry
    assert re.search(r"class Empty\(NamedTuple\):\s+pass", entry)


def test_generate_pattern_file_covers_every_namedtuple_class():
    text = generate_pattern_file(nbnt_example_hello)
    for cls_name in NT_CLASSES:
        assert re.search(
            rf"class {cls_name}\(NamedTuple\):", text
        ), f"missing NamedTuple block for {cls_name}"
    assert text.count("$:") == len(NT_CLASSES)


def test_is_namedtuple_class_rejects_unrelated_types():
    assert not is_namedtuple_class(int)
    assert not is_namedtuple_class(tuple)
    assert not is_namedtuple_class(nbnt_example_hello.hello)

    from collections import namedtuple

    Plain = namedtuple("Plain", ["x", "y"])
    assert not is_namedtuple_class(Plain)


@pytest.fixture(scope="module")
def generated_stub(tmp_path_factory):
    """Pattern file + vanilla ``nanobind.stubgen`` run against the extension."""
    tmp_path = tmp_path_factory.mktemp("stubgen")
    pattern_file = tmp_path / "hello.pat"
    stub_file = tmp_path / "hello.pyi"

    subprocess.run(
        [
            sys.executable,
            "-m",
            "nanobind_namedtuple_stubgen",
            "-m",
            MODULE_NAME,
            "-o",
            str(pattern_file),
        ],
        check=True,
    )
    subprocess.run(
        [
            sys.executable,
            "-m",
            "nanobind.stubgen",
            "-m",
            MODULE_NAME,
            "-p",
            str(pattern_file),
            "-o",
            str(stub_file),
            "-q",
        ],
        check=True,
    )
    return stub_file.read_text(encoding="utf-8")


def test_end_to_end_stub_contains_namedtuple_blocks(generated_stub):
    assert "from typing import NamedTuple" in generated_stub
    for cls_name in NT_CLASSES:
        assert re.search(
            rf"class {cls_name}\(NamedTuple\):", generated_stub
        ), f"missing NamedTuple block for {cls_name}"


def test_end_to_end_stub_has_no_tuple_rendering(generated_stub):
    assert "(tuple)" not in generated_stub
    assert "_tuplegetter" not in generated_stub
    assert "Alias for field number" not in generated_stub


def test_end_to_end_stub_preserves_docstrings_and_defaults(generated_stub):
    assert '"""A 2D point with an optional display label."""' in generated_stub
    assert re.search(r"label: str = ['\"]{2}", generated_stub)
    assert '"""X coordinate."""' in generated_stub


def test_end_to_end_stub_keeps_function_signatures_typed(generated_stub):
    assert re.search(r"def make_color\(.*\) -> Color:", generated_stub)
    assert re.search(r"def sum_vec3\(arg: Vec3, /\) -> float:", generated_stub)
