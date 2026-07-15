# Internal design

This document explains how `nanobind_namedtuple` is put together and why it is
built the way it is. It assumes familiarity with nanobind's type-caster
machinery. Everything described here lives in
`include/nanobind_namedtuple/named_tuple.h` and
`stubgen/nanobind_namedtuple_stubgen/`.

## End-to-end shape

A declaration macro such as `NB_NAMED_TUPLE(Color, r, g, b)` expands, at file
scope, into two template specializations: `nbnt::detail::traits<Color>`, which
records the Python class name and a compile-time tuple of field descriptors,
and `nanobind::detail::type_caster<Color>`, which forwards `from_python` /
`from_cpp` to shared helpers parameterized on those traits. At import time,
`nbnt::bind_namedtuple<Color>(m)` walks the descriptors to build the field-name
tuple, trailing defaults, and per-field annotation strings, calls
`collections.namedtuple` to create the class, decorates it, publishes it into a
per-type static slot with compare-and-swap semantics, and attaches it to the
module. From then on the caster converts C++ values into instances of that
class and accepts both those instances and plain tuples on the way in.

The split matters: everything that can be decided at compile time (field
names, member pointers, default presence, annotation text) is decided at
compile time, so the only per-conversion runtime work is one pointer load, one
tuple allocation, and the per-field child casts.

## Zero-STL posture

The header includes `<nanobind/nanobind.h>` and, only under
`NB_FREE_THREADED`, `<atomic>`. Nothing else. Since this is a header consumers
include into every binding TU, avoiding `<string>`, `<tuple>`, `<functional>`
and friends keeps compile times and symbol bloat down, and avoids dragging STL
container casters into scope that the user did not ask for.

Three substitutions make this work. Strings are `const char *` throughout:
field names and docstrings come from string literals whose lifetime is the
program's, so owning strings buy nothing. `std::tuple` + `std::apply` is
replaced by `nanobind::detail::tuple` (already available via the nanobind
include) expanded with `std::index_sequence` folds — `field_pack_size` stands
in for `std::tuple_size_v`, and `for_each_field` / `make_names_object` stand in
for `std::apply`. Finally, annotation sanitization writes into a `constexpr`
fixed-size `annot_text<N>` char buffer sized from the caster's `Name.text`
array, so no dynamic allocation or string type is needed at all.

## Field descriptors

`nbnt::field<MemberPtr, HasDefault>` carries the member pointer and name as
template/constexpr data. There are two specializations rather than one struct
with an optional payload: the `HasDefault = false` primary holds only two
`const char *` members and is `constexpr`-constructible; calling `.default_(v)`
on it returns a *different type*, `field<MemberPtr, true>`, which stores
a materialized `value_type default_value`. Encoding default-presence in the
type is what lets `bind_namedtuple` validate default placement at compile
time: `defaults_are_trailing<Fields>()` folds over `has_default_v` of each
descriptor type and a `static_assert` rejects any field list where a
defaultless field follows a defaulted one — the same rule
`collections.namedtuple` enforces at runtime, surfaced as a compile error
instead. (`make_defaults_tuple_head` is written to stay well-formed even for
invalid orders, returning `none()`, precisely so the `static_assert` is the
first diagnostic the user sees rather than a template error inside it.)

`.doc("...")` exists on both specializations and just returns a copy with
`doc_str` set, so `.default_()` and `.doc()` chain in either order.

## Macro layer

All three declaration macros funnel into `NB_NAMED_TUPLE_EX(Type, ClassName,
descriptors...)`; it is the single expansion path that emits the `traits` and
`type_caster` specializations, and also the documented escape hatch for
anything the sugar cannot express.

The sugar (`NB_NAMED_TUPLE`, `NB_NAMED_TUPLE_AS`) has to map bare member names
onto `nbnt::field<&Type::member>("member")` descriptors. That is the classic
FOR_EACH problem, solved the classic way: `NB_NT_SEL` returns its 26th
argument, and each dispatcher appends a reversed table of macro names so the
total argument count selects the right fixed-arity expander (`NB_NT_FE1` …
`NB_NT_FE16`). The deliberate twist is that arity selection happens on the
*total* argument count of the outer macro, not on the field list alone:
`NB_NAMED_TUPLE(Empty)` — one argument — selects `NB_NT_NT_0`, which expands
to `NB_NAMED_TUPLE_EX(Type, #Type, )` with an empty descriptor list. This
sidesteps the notorious zero-varargs edge case: there is no portable pre-C++20
way for a FOR_EACH to distinguish "no varargs" from "one empty vararg", but
counting the fixed leading arguments together with the fields makes the empty
case just another arity. `NB_NT_EXPAND` wraps each selection to force the
extra rescan MSVC's traditional preprocessor needs.

The bare-name forms cap out at 16 fields because each arity needs a hand-
written `NB_NT_FE<k>` macro. Positions 17–25 of the selection table map to
`NB_NT_FE_TOO_MANY`, which instantiates `too_many_fields<>` — a struct whose
`static_assert` names the cap and points at `NB_NAMED_TUPLE_EX`, which has no
limit since the user writes the descriptors out themselves.

## Runtime publication

The registered Python class for `T` lives in `nt_class<T>::cls`, an inline
static per-type slot. Under the GIL it is a plain `PyObject *` (registration
is serialized anyway); under `NB_FREE_THREADED` it is accessed through
`std::atomic_ref` (or `std::atomic` where `atomic_ref` is unavailable), with a
`static_assert` requiring lock-freedom. Because the slot is an inline variable
in a header, extension modules that share the symbol at dynamic-link time also
share the slot, which is what makes a `T` bound by two modules interoperable.

Publication is exactly-once by construction. `bind_namedtuple` first does a
cold acquire-load; if a class already exists it only reattaches it as a module
attribute and returns. Otherwise it builds a complete candidate — namedtuple
class, sentinels, `__doc__`, per-field property docstrings — and only then
attempts a release/acquire CAS from null. The loser decrefs its candidate and
adopts the winner. Applying docstrings and annotations to the candidate
*before* the CAS is deliberate: the CAS is the linearization point, so no
thread can ever observe a published class in a half-decorated state, and a
losing registration's docs are simply never observable.

Two smaller decisions in `nt_finalize_class` are worth noting. The
`collections.namedtuple` factory is cached in a function-local static whose
reference is intentionally leaked, so no static destructor touches Python
state after `Py_Finalize`. And the class gets *two* annotation attributes:
`__nb_nt_annotations__` holds the dict the stubgen relies on, while
`__annotations__` is set to a `PyDict_Copy` of it. `__annotations__` is a
conventional, user-mutable attribute; giving it a distinct dict means user
code that pokes at it cannot silently corrupt the stub-generation metadata.

Conversion itself avoids calling the class. `from_cpp` allocates a bare tuple
with `PyTuple_New`, fills the slots via each field's caster, then retypes it
with `Py_SET_TYPE` to the registered class — one allocation, no argument
packing, no `__new__` dispatch. This is safe because a `collections.namedtuple`
subclass adds no instance state beyond the tuple items. Zero-field types take
the plain call path since there is nothing to fill. On the policy side,
`reference` / `automatic_reference` are downgraded to `copy` (or `move` for
rvalue sources) because a tuple cannot alias the parent object, and
`reference_internal` / `take_ownership` raise `TypeError` up front rather than
constructing something with dangling semantics. `from_python` is strict: the
input must be exactly the registered class or exactly a plain `tuple` of
matching arity — subclasses, lists, and other sequences are rejected without
raising, leaving overload resolution to other candidates.

## Annotation generation

Per-field annotation strings are derived from nanobind itself: for a field
type `F`, `make_caster<F>::Name` is the same compile-time descr nanobind uses
to render signatures, so annotations automatically agree with function stubs
(`float`, `str`, nested namedtuple names, `list[tuple[int, int]]`, …). Casters
whose `Name` contains type-substitution slots (`type_count() != 0`, e.g.
`nb::typed<...>` placeholders) cannot be rendered without runtime types, so
they fall back to `"typing.Any"`.

Raw `Name.text` is not directly usable, though: casters like `optional`'s
embed `@input@output@` stubgen markers describing different input/output
spellings. `sanitize_annotation` collapses each marker pair to its output side
at compile time — matching how nanobind renders return values — into a
`static constexpr` buffer, so `field_annotation_str` returns a pointer to
fully-sanitized static text with zero runtime cost.

These strings are stored, not evaluated — until someone calls
`typing.get_type_hints()`, which evaluates them in the *extension module's*
namespace. A hand-written module carrying `Optional[float]` annotations would
have an `import typing` at its top; a C extension has no such statement. So
when any annotation references `typing.` — the `typing.Any` fallback, or any
optional field on Python 3.9, where nanobind spells optionals
`typing.Optional[float]` rather than the 3.10+ `float | None` —
`bind_namedtuple` injects `m.attr("typing") = import_("typing")` (unless the
attribute already exists), and `get_type_hints()` resolves.

## Stubgen

`NamedTupleStubGen` subclasses `nanobind.stubgen.StubGen` and overrides one
method, `put_type`. Detection uses the `__nb_named_tuple__` sentinel attached
at bind time; anything without it falls through to the parent emitter, as do
aliases and cross-module references (mismatched name/module), mirroring
nanobind's own handling of those cases. For recognized classes it emits a
`class X(typing.NamedTuple):` block, reading field order from `_fields`,
annotation strings from `__nb_nt_annotations__`, and defaults from
`_field_defaults` — i.e. everything comes from the runtime class, so the stub
cannot drift from what was actually bound. Dotted names inside annotation
strings are routed through `import_object` so the generated `.pyi` gets
correct imports.

Docstring emission has one subtlety: `collections.namedtuple` synthesizes a
class docstring (`"Color(r, g, b)"`) and per-field docstrings (`"Alias for
field number 0"`) on every class it creates. To distinguish those from docs
the user actually set via `.doc()` / `bind_namedtuple`'s `cls_doc`, the
generator builds a throwaway reference `collections.namedtuple(name, fields)`
and emits a docstring only when it differs from the reference's. That avoids
hard-coding CPython's synthetic phrasing, which is not a stable contract.

The `__main__` entry point reuses `nanobind.stubgen.main()` wholesale,
temporarily swapping `StubGen` for `NamedTupleStubGen` — same CLI, one changed
class, restored on exit.

## Rejected alternatives

*Binding via `nb::class_` (or a hand-rolled tuple subclass) instead of a real
`collections.namedtuple` class.* A nanobind-managed class would integrate more
tightly with nanobind's type registry, but the whole point of the library is
that consumers get the namedtuple contract — `_fields`, `_asdict`, `_replace`,
`__match_args__`, pickle — for free and exactly as CPython defines it.
Delegating class creation to the stdlib factory means none of that surface has
to be reimplemented or kept compatible by hand.

*Constructing instances by calling the class.* The obvious `from_cpp` would be
`cls(*items)`. The single-allocation `PyTuple_New` + `Py_SET_TYPE` path was
chosen instead from the moment the registered class existed (the commit
introducing `bind_namedtuple` names it the "single-allocation from_cpp fast
path") because the call path pays for argument-tuple packing and namedtuple's
`__new__` on every C++-to-Python conversion of what is meant to be a cheap
value type.

*A runtime type registry keyed by `std::type_index` instead of per-`T` static
slots and baked-in caster names.* A map would need `<unordered_map>`, locking,
and a runtime lookup on every conversion and every signature rendering. The
per-type `nt_class<T>` slot makes the hot-path lookup a single relaxed load,
and `NB_TYPE_CASTER(Type, const_name(ClassName))` bakes the Python-visible
name into the caster at compile time, so docstrings and stubs render correctly
even before (or without) registration.

*Sharing one dict between `__annotations__` and the stubgen sentinel.* The
first implementation did; it was split ("give `__annotations__` its own dict")
once it became clear that `__annotations__` is conventionally mutable and a
user mutation would corrupt stub output.

*Lenient `from_python`.* Subclass-tolerant checks (`PyTuple_Check`) or general
sequence acceptance would let the caster coerce shapes it does not own. The
implementation instead pairs two *exact* checks — `Py_TYPE(src)` equal to the
registered class, or `PyTuple_CheckExact` — plus an exact arity match, so the
caster composes predictably inside overload sets and mismatches fall through
to other candidates without raising.
