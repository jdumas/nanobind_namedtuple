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

NB_NAMED_TUPLE(Color, r, g, b)

NB_MODULE(mymod, m) {
    nbnt::bind_namedtuple<Color>(m);
    m.def("make_color", [](float r, float g, float b) { return Color{r, g, b}; });
}
```

The `Color` values returned from C++ become instances of the class produced by
`collections.namedtuple("Color", ("r", "g", "b"))` — with `_fields`,
`_asdict`, `_replace`, structural pattern matching, and pickle inherited for
free. `Color` also accepts plain-tuple inputs of matching arity from Python.

## Declaration macros

Three declaration macros cover increasing levels of control:

- `NB_NAMED_TUPLE(Type, fields...)` — the common case. Takes bare member
  names; the Python class name is the stringified `Type`:

  ```cpp
  NB_NAMED_TUPLE(Color, r, g, b)   // Python class "Color"
  NB_NAMED_TUPLE(Empty)            // empty field list is valid
  ```

- `NB_NAMED_TUPLE_AS(Type, "Name", fields...)` — same as above with an
  explicit Python class name. Use it for qualified C++ types whose
  stringification is not a valid Python identifier:

  ```cpp
  NB_NAMED_TUPLE_AS(geom::Point, "Point", x, y)
  ```

- `NB_NAMED_TUPLE_EX(Type, "Name", descriptors...)` — full descriptor form.
  Each field is an explicit `nbnt::field<&Type::member>("member")`, which
  supports `.default_(value)` and `.doc("...")` (chainable in either order);
  `NB_NT_FIELD(member)` is shorthand for a plain descriptor:

  ```cpp
  NB_NAMED_TUPLE_EX(
      Point, "Point",
      NB_NT_FIELD(x),
      nbnt::field<&Point::y>("y").default_(0).doc("Y coordinate."))
  ```

`NB_NAMED_TUPLE` and `NB_NAMED_TUPLE_AS` accept at most 16 bare field names —
beyond that a `static_assert` points at `NB_NAMED_TUPLE_EX`, which has no cap.

## Nested records

Fields whose types are themselves `NB_NAMED_TUPLE`-bound records nest
recursively; register the inner type before any outer type that references
it in a field:

```cpp
struct Vec3 { float x, y, z; };
struct Pixel { Vec3 position; Color color; };

NB_NAMED_TUPLE(Vec3, x, y, z)
NB_NAMED_TUPLE(Pixel, position, color)

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

`nbnt::bind_namedtuple<T>` produces value casters. `rv_policy::reference`
and `rv_policy::automatic_reference` are downgraded to `copy`, or to `move`
for rvalue sources, because the resulting tuple cannot reference the parent
object. `rv_policy::reference_internal` and `rv_policy::take_ownership`
raise `TypeError` at cast time.

## CMake integration

The library is header-only. Consumers link the imported target
`nanobind_namedtuple::nanobind_namedtuple`, which propagates the include
directory and `cxx_std_17`.

### Prerequisites

- CMake ≥ 3.15 and a C++17 compiler.
- Python ≥ 3.9 with `nanobind` installed in the interpreter CMake picks up
  (`pip install nanobind`). The top-level `CMakeLists.txt` locates
  nanobind's CMake package by running `python -m nanobind --cmake_dir` on
  `Python_EXECUTABLE`, so no extra `CMAKE_PREFIX_PATH` entry is needed.
  Pass `-DPython_EXECUTABLE=/path/to/python` at configure time to select a
  specific interpreter.

### FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    nanobind_namedtuple
    GIT_REPOSITORY https://github.com/jdumas/nanobind_namedtuple.git
    GIT_TAG        main)
FetchContent_MakeAvailable(nanobind_namedtuple)

nanobind_add_module(my_ext STABLE_ABI my_ext.cpp)
target_link_libraries(my_ext PRIVATE nanobind_namedtuple::nanobind_namedtuple)
```

Pin `GIT_TAG` to a commit hash or release tag for reproducible builds.

### CPM.cmake

```cmake
CPMAddPackage(
    NAME             nanobind_namedtuple
    GITHUB_REPOSITORY jdumas/nanobind_namedtuple
    GIT_TAG          main)

target_link_libraries(my_ext PRIVATE nanobind_namedtuple::nanobind_namedtuple)
```

Both routes execute the top-level `CMakeLists.txt`, which reuses Python and
nanobind from the parent project when they are already available: Python
discovery is skipped when the `Python::Module` target exists, and nanobind
discovery (including the `python -m nanobind --cmake_dir` probe) is skipped
when `nanobind_add_module` is already defined, a `nanobind`/`nanobind-static`
target exists, or `nanobind_FOUND` is set. Otherwise it runs its own
`find_package(Python 3.9 COMPONENTS Interpreter Development.Module REQUIRED)`
and `find_package(nanobind CONFIG REQUIRED)`. The example extension is
guarded by `NBNT_BUILD_EXAMPLES` (default `OFF`), so consuming projects do
not build it.
