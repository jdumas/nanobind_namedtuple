# stubgen

Pattern-file generator that makes vanilla nanobind stubgen emit canonical
``typing.NamedTuple`` stubs for classes registered with
``nbnt::bind_namedtuple<T>``.

## How it works

``bind_namedtuple<T>`` attaches two sentinel attributes to the class it
registers:

* ``__nb_named_tuple__`` — always ``True``.
* ``__nb_nt_annotations__`` — ordered mapping from field name to a
  Python-syntax annotation string. Built-in scalars, ``nb::object``, and
  nested ``NB_NAMED_TUPLE``-bound types resolve to their public name; fields
  whose caster carries type-substitution slots fall back to ``typing.Any``.

Default values come from ``_field_defaults`` (inherited from
``collections.namedtuple``).

``python -m nanobind_namedtuple_stubgen`` imports the built extension, finds
sentinel-marked classes defined in it, and writes a nanobind stubgen
[pattern file](https://nanobind.readthedocs.io/en/latest/typing.html#pattern-files)
with one entry per class — an anchored regex on the fully-qualified name,
whose replacement is the canonical block:

```
^my_ext\.Color$:
    \from typing import NamedTuple
    class Color(NamedTuple):
        r: float
        g: float
        b: float
```

Vanilla ``python -m nanobind.stubgen`` (or CMake's ``nanobind_add_stub()``)
consumes that file through its standard ``-p`` / ``PATTERN_FILE`` option and
substitutes the block for its default ``class X(tuple): ...`` rendering.
Everything else in the stub — functions, signatures, unrelated classes — is
produced by nanobind untouched.

## Command-line use

```
python -m nanobind_namedtuple_stubgen -m my_ext -o my_ext.pat
python -m nanobind.stubgen -m my_ext -p my_ext.pat -o my_ext.pyi
```

Options: ``-m MODULE`` (repeatable), ``-i PATH`` to extend the import path
(repeatable), ``-r`` to recurse into submodules, and ``-o FILE`` for the
output pattern file.

## CMake use

Generate the pattern file after building the extension and hand it to the
standard ``nanobind_add_stub()``:

```cmake
add_custom_command(
    OUTPUT my_ext.pat
    COMMAND Python::Interpreter -m nanobind_namedtuple_stubgen
            -i $<TARGET_FILE_DIR:my_ext> -m my_ext -o my_ext.pat
    DEPENDS my_ext)
add_custom_target(my_ext_pattern DEPENDS my_ext.pat)

nanobind_add_stub(
    my_ext_stub
    MODULE my_ext
    OUTPUT my_ext.pyi
    PATTERN_FILE my_ext.pat
    PYTHON_PATH $<TARGET_FILE_DIR:my_ext>
    DEPENDS my_ext my_ext_pattern)
```

## Programmatic use

```python
from nanobind_namedtuple_stubgen import generate_pattern_file
import my_extension

print(generate_pattern_file(my_extension))
```

## Bypass

The generator only writes a pattern file; skipping it leaves nanobind's
default ``class X(tuple): ...`` rendering in place. Hand-written entries can
be appended to the generated file (or kept in a separate pattern file) to
override individual classes.
