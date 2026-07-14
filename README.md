# nanobind_namedtuple

`namedtuple` support for [nanobind](https://github.com/wjakob/nanobind).

## Quick start

Declare a `NB_NAMED_TUPLE` at file scope, then register it inside `NB_MODULE`:

```cpp
#include <nanobind_namedtuple/named_tuple.h>

struct Color {
    float r;
    float g;
    float b;
};

NB_NAMED_TUPLE(Color, "Color", NB_NT_FIELD(r), NB_NT_FIELD(g), NB_NT_FIELD(b))

NB_MODULE(mymod, m) {
    nbnt::bind_namedtuple<Color>(m);
    m.def("make_color", [](float r, float g, float b) { return Color{r, g, b}; });
}
```

The `Color` values returned from C++ become instances of the class produced by
`collections.namedtuple("Color", ("r", "g", "b"))` — with `_fields`,
`_asdict`, `_replace`, structural pattern matching, and pickle inherited for
free. `Color` also accepts plain-tuple inputs of matching arity from Python.

## Nested records

Fields whose types are themselves `NB_NAMED_TUPLE`-bound records nest
recursively; register the inner type before any outer type that references
it in a field:

```cpp
struct Vec3 { float x, y, z; };
struct Pixel { Vec3 position; Color color; };

NB_NAMED_TUPLE(Vec3,  "Vec3",  NB_NT_FIELD(x), NB_NT_FIELD(y), NB_NT_FIELD(z))
NB_NAMED_TUPLE(Pixel, "Pixel", NB_NT_FIELD(position), NB_NT_FIELD(color))

NB_MODULE(mymod, m) {
    nbnt::bind_namedtuple<Vec3>(m);
    nbnt::bind_namedtuple<Color>(m);
    nbnt::bind_namedtuple<Pixel>(m);
    // ...
}
```

## Optional fields

`std::optional<T>` fields work through
[`nanobind/stl/optional.h`](https://github.com/wjakob/nanobind/blob/master/include/nanobind/stl/optional.h);
`std::nullopt` maps to Python `None` and an engaged value round-trips as the
underlying `T`:

```cpp
#include <nanobind/stl/optional.h>

struct Tagged { int value; std::optional<int> tag; };

NB_NAMED_TUPLE_EX(
    Tagged, "Tagged",
    nbnt::field<&Tagged::value>("value"),
    nbnt::field<&Tagged::tag>("tag").default_(std::optional<int>{}))
```

## Return-value policy

`nbnt::bind_namedtuple<T>` produces value casters. The caster silently
downgrades `rv_policy::reference` and `rv_policy::automatic_reference` to
`copy` (or `move`, for rvalue sources) because a tuple never legally
references the internals of a soon-to-die parent temporary.
`rv_policy::reference_internal` and `rv_policy::take_ownership` are rejected
with a clean `TypeError` at cast time — tuple instances lack `__weakref__`
for lifetime tracking and have no owned C++ instance to transfer.
