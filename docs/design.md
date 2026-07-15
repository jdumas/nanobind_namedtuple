# Internal design

This document explains how `nanobind_namedtuple` is put together and why it is
built the way it is. It assumes familiarity with nanobind's type-caster
machinery. Everything described here lives in
`include/nanobind_namedtuple/named_tuple.h`,
`stubgen/nanobind_namedtuple_stubgen/`, and
`cmake/nanobind_namedtuple.cmake`.

## Goals and constraints

Five requirements shape the design:

- **Real namedtuples.** A bound type must be an actual
  `collections.namedtuple` class, so consumers get `_fields`, `_asdict`,
  `_replace`, `__match_args__`, and pickling exactly as CPython defines them.
- **Header-only, zero-STL.** The header includes `<nanobind/nanobind.h>` and,
  only under `NB_FREE_THREADED`, `<atomic>` — nothing else. It is included
  into every binding TU, so STL headers stay out to protect compile times and
  to avoid pulling STL container casters into scope.
- **Two-way conversion plus stubs.** C++ values convert to namedtuple
  instances; those instances and plain tuples convert back; a stubgen
  extension renders `typing.NamedTuple` blocks.
- **One class per type.** A `T` bound by several extension modules must map
  to a single Python class, so instances interoperate across modules.
- **Free-threading support.** Registration must be safe without the GIL;
  under the GIL, the GIL itself is the synchronization.

The pipeline: a declaration macro such as `NB_NAMED_TUPLE(Color, r, g, b)`
expands at file scope into `nbnt::detail::traits<Color>` (the Python class
name plus a compile-time tuple of field descriptors) and
`nanobind::detail::type_caster<Color>`. The declaration fixes member pointers,
field names, default presence, and annotation text at compile time. At import
time, `nbnt::bind_namedtuple<Color>(m)` converts that metadata into Python
objects, calls `collections.namedtuple`, decorates the result, publishes it
into a per-type static slot, and attaches it to the module. Conversions then
go through that slot, as described below.

## Declaration metadata

`nbnt::field<MemberPtr, HasDefault>` carries the member pointer and name as
template/constexpr data, in two specializations: the `HasDefault = false`
primary holds only two `const char *` members, and calling `.default_(v)` on
it returns a *different type*, `field<MemberPtr, true>`, which adds a
materialized `value_type default_value`. Encoding default-presence in the
type lets a `static_assert` on `defaults_are_trailing<Fields>()` reject any
field list where a defaultless field follows a defaulted one — the rule
`collections.namedtuple` enforces at runtime, surfaced at compile time
instead. `.doc("...")` exists on both specializations, so `.default_()` and
`.doc()` chain in either order.

All strings in this layer are non-owning `const char *`. The sugar macros
always pass string literals, but `NB_NAMED_TUPLE_EX` and `.doc()` accept
arbitrary pointers; the pointed-to text must remain valid for the life of the
program.

Two further substitutions keep the header zero-STL: `std::tuple` +
`std::apply` is replaced by `nanobind::detail::tuple` expanded with
`std::index_sequence` folds, and annotation sanitization writes into a
`constexpr` fixed-size `annot_text<N>` char buffer sized from the caster's
`Name.text` array, so no dynamic allocation or string type is needed.

## Class identity and lifetime

The registered Python class for `T` lives in `nt_class<T>::cls`, an inline
static per-type slot. Because the slot is an inline variable in a header,
extension modules that share the symbol at dynamic-link time also share the
slot; class identity across modules depends on the loader coalescing that
symbol.

Publication differs by build. Under the GIL the slot is a plain `PyObject *`:
`bind_namedtuple` checks it for null and assigns, and the GIL serializes
registrations, so no atomics are involved. Under `NB_FREE_THREADED` the slot
is accessed through `std::atomic_ref` (or `std::atomic` where `atomic_ref` is
unavailable), with a `static_assert` requiring lock-freedom: `bind_namedtuple`
does a cold acquire-load and, if a class already exists, only reattaches it as
a module attribute; otherwise it builds a complete candidate (namedtuple
class, sentinels, `__doc__`, per-field property docstrings) and attempts a
release CAS from null, decrefing the candidate and adopting the winner if it
loses. Decorating *before* publication means the slot only ever holds fully
decorated classes and a losing registration's docs are never observable.

First registration wins; the slot holds a strong reference that is never
released, so the class lives for the remainder of the process. Likewise, the
`collections.namedtuple` factory is cached in a leaked function-local static
so no static destructor touches Python state after `Py_Finalize`. On
free-threaded builds the conversion hot path reads the slot with a relaxed
load, which does not by itself synchronize with the publisher's stores;
correctness additionally relies on module initialization completing — and the
module reaching the calling thread through Python's own synchronization —
before any conversion runs. Subinterpreters, module unloading, and
interpreter re-initialization are unsupported.

## Conversion

C++→Python (`from_cpp`) avoids calling the class for non-empty types: it
loads the registered class from the slot, allocates a bare tuple with
`PyTuple_New`, fills the slots via each field's caster, then retypes the
tuple with `Py_SET_TYPE` — one allocation, no argument packing, no `__new__`
dispatch. This is safe because a `collections.namedtuple` subclass adds no
instance state beyond the tuple items. Zero-field types take the plain call
path. On the policy side, `reference` / `automatic_reference` are downgraded
to `copy` (or `move` for rvalue sources) because a tuple cannot alias the
parent object, and `reference_internal` / `take_ownership` raise `TypeError`
up front rather than constructing something with dangling semantics.

Python→C++ (`from_python`) is strict: the input must be exactly the
registered class or exactly a plain `tuple` of matching arity — subclasses,
lists, and other sequences are rejected without raising, leaving overload
resolution to other candidates. On success the caster default-constructs a
`T` (a `static_assert` requires `std::is_default_constructible_v<Type>`) and
assigns each member through its member pointer from the corresponding tuple
item's caster.

## Annotation generation

Per-field annotation strings are derived from nanobind itself: for a field
type `F`, `make_caster<F>::Name` is the same compile-time descr nanobind uses
to render signatures, so the annotation agrees with function stubs exactly
(`float`, `str`, nested namedtuple names, `list[tuple[int, int]]`, …).
Casters whose `Name` contains runtime type-substitution slots
(`type_count() != 0`, e.g. `nb::typed<...>` placeholders) cannot be rendered
without runtime types and fall back to `"typing.Any"`.

Raw `Name.text` is not directly usable: casters like `optional`'s embed
`@input@output@` stubgen markers describing different input/output spellings.
`sanitize_annotation` collapses each marker pair to its output side at
compile time — matching how nanobind renders return values — into a
`static constexpr` buffer, so annotations cost nothing at runtime.

The class gets *two* annotation attributes: `__nb_nt_annotations__` holds the
dict the stubgen relies on, while `__annotations__` is set to a `PyDict_Copy`
of it. `__annotations__` is conventionally user-mutable; a distinct dict means
mutating it cannot corrupt the stub-generation metadata.

Annotation strings are stored, not evaluated — until someone calls
`typing.get_type_hints()`, which evaluates them in the *extension module's*
namespace, where no `import typing` exists. So when any annotation references
`typing.` — the `typing.Any` fallback, or optional fields on Python 3.9,
spelled `typing.Optional[float]` rather than the 3.10+ `float | None` —
`bind_namedtuple` injects `m.attr("typing") = import_("typing")` (unless the
attribute already exists), and `get_type_hints()` resolves.

## Stubgen

The stubgen package works exclusively in *pattern-file mode*: it never emits
stubs itself. It imports a built extension, finds the classes carrying the
`__nb_named_tuple__` sentinel (restricted to classes actually defined in the
scanned module — name and `__module__` must both match, so aliases and
re-exports are skipped), and writes a nanobind stubgen pattern file. Vanilla
`python -m nanobind.stubgen` (or `nanobind_add_stub()`) consumes it through
the standard `-p` / `PATTERN_FILE` option and substitutes each entry's
replacement block for its default `class X(tuple): ...` rendering.

Pattern files are the deliberate choice of integration point. They are
nanobind's official last-resort mechanism for customizing generated stubs,
with a documented file format; subclassing `nanobind.stubgen.StubGen`, the
previous approach, hooks an experimental API that is explicitly outside
nanobind's semantic-versioning guarantees. The other sanctioned tool,
`nb::sig`, overrides signatures of bound functions and classes — it has no
purchase here because a registered namedtuple is a pure-Python
`collections.namedtuple` class, not a nanobind type object. With the pattern
file, nanobind's stubgen remains the sole emitter, and the generator's only
contract is the documented file syntax.

Each entry is an anchored, dot-escaped regex on the fully-qualified class
name, followed by an indented replacement: `\from typing import NamedTuple`
escape lines to register imports, then the `class X(NamedTuple):` block with
field order from `_fields`, annotation strings from `__nb_nt_annotations__`,
and defaults from `_field_defaults` — everything comes from the runtime class
rather than the C++ declaration. Modules referenced by dotted names inside
annotation strings are registered with `\import` escape lines.

`collections.namedtuple` synthesizes a class docstring (`"Color(r, g, b)"`)
and per-field docstrings (`"Alias for field number 0"`) on every class it
creates. To distinguish those from docs the user actually set, the generator
builds a throwaway reference `collections.namedtuple(name, fields)` and emits
a docstring only when it differs from the reference's — CPython's synthetic
phrasing is not a stable contract to hard-code.

The `__main__` entry point takes `-m MODULE` (repeatable), `-i` import-path
entries, `-r` for submodule recursion, and `-o` for the output pattern file —
mirroring the corresponding `nanobind.stubgen` options so the two commands
can share arguments in build scripts.

For CMake consumers, `cmake/nanobind_namedtuple.cmake` (included by the
top-level `CMakeLists.txt`, so the function exists after any integration
route) defines `nanobind_namedtuple_stub_pattern()`. It assembles the command
line above — resolving the generator package's location from the module's own
file at include time (`CMAKE_CURRENT_LIST_DIR` captured into an internal cache
variable, keeping the project's CMake 3.15 floor) and prepending it to any
ambient `PYTHONPATH`, so consumers never configure import paths for the
generator — and registers it
either as an `add_custom_command`/target pair (build time) or as an
`install(CODE)` rule with `COMMAND_ERROR_IS_FATAL ANY` (install time, for
scikit-build-core layouts). Argument names deliberately mirror
`nanobind_add_stub()`. The helper never wraps or calls `nanobind_add_stub()`:
consumers keep calling the official function and pass the generated file as
`PATTERN_FILE`. Ordering falls out of existing CMake semantics — the
build-time custom command is a file-level dependency of the stub rule, and
install rules run in declaration order, so an `INSTALL_TIME` helper call
declared before the `INSTALL_TIME` stub call runs first. Because entries are
anchored, fully-qualified queries applied first-match-wins, pattern files
from several producers concatenate safely.

## Macro layer

All three declaration macros funnel into `NB_NAMED_TUPLE_EX(Type, ClassName,
descriptors...)`, the single expansion path and the documented escape hatch
for anything the sugar cannot express. The sugar maps bare member names onto
`nbnt::field` descriptors with a classic FOR_EACH: a selection macro picks a
fixed-arity expander from the total argument count. Counting the fixed
leading arguments together with the fields makes the zero-field case just
another arity, sidestepping the pre-C++20 inability to distinguish "no
varargs" from "one empty vararg"; `NB_NT_EXPAND` forces the extra rescan
MSVC's traditional preprocessor needs. The bare-name forms cap at 16 fields —
each arity needs a hand-written expander — and overflow selects a
`static_assert` pointing at `NB_NAMED_TUPLE_EX`, which has no limit. The
token-level mechanics are documented in the header itself.

## Rejected alternatives

*Binding via `nb::class_` (or a hand-rolled tuple subclass).* A
nanobind-managed class would integrate more tightly with nanobind's type
registry, but the namedtuple contract would have to be reimplemented and kept
compatible by hand; the stdlib factory gets that surface for free.

*Constructing instances by calling the class,* i.e. `cls(*items)` in
`from_cpp`. The call path pays for argument-tuple packing and namedtuple's
`__new__` on every conversion of what is meant to be a cheap value type.

*A runtime type registry keyed by `std::type_index`.* A map would need
`<unordered_map>`, locking, and a runtime lookup on every conversion. The
per-type `nt_class<T>` slot makes the lookup a single load, and
`NB_TYPE_CASTER(Type, const_name(ClassName))` bakes the Python-visible name
into the caster at compile time, so docstrings and stubs render correctly
even before (or without) registration.

*Lenient `from_python`.* Subclass-tolerant checks (`PyTuple_Check`) or
general sequence acceptance would let the caster coerce shapes it does not
own, making overload resolution unpredictable.
