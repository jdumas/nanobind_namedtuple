# stubgen

Nanobind stubgen extension that emits canonical ``typing.NamedTuple`` stubs
for classes registered with ``nbnt::bind_namedtuple<T>``.

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

``nanobind_namedtuple_stubgen.NamedTupleStubGen`` subclasses
``nanobind.stubgen.StubGen`` and overrides ``put_type`` to recognise the
sentinel. It emits, for each registered type:

```python
class Color(typing.NamedTuple):
    r: float
    g: float
    b: float
```

Types without the sentinel fall through to nanobind's default emitter.

## Command-line use

```
python -m nanobind_namedtuple_stubgen -m nbnt_example_hello -o hello.pyi
```

Accepts the same options as ``python -m nanobind.stubgen``.

## Programmatic use

```python
from nanobind_namedtuple_stubgen import NamedTupleStubGen
import my_extension

gen = NamedTupleStubGen(module=my_extension)
gen.put(my_extension)
print(gen.get())
```

## Bypass

If you do not want the hook, either:

* Write ``.pyi`` files by hand and add the module (or its namedtuple
  attributes) to nanobind stubgen's pattern-file ignore list, or
* Invoke plain ``python -m nanobind.stubgen`` and post-process the output.
