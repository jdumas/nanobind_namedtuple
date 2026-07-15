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

### Git submodule + `add_subdirectory`

```
git submodule add https://github.com/jdumas/nanobind_namedtuple.git \
    third_party/nanobind_namedtuple
```

```cmake
add_subdirectory(third_party/nanobind_namedtuple)
target_link_libraries(my_ext PRIVATE nanobind_namedtuple::nanobind_namedtuple)
```

All three routes execute the top-level `CMakeLists.txt`, which runs its own
`find_package(Python 3.9 COMPONENTS Interpreter Development.Module REQUIRED)`
and `find_package(nanobind CONFIG REQUIRED)`. If the parent project already
called `find_package(Python ...)` with a compatible component set, CMake
reuses the cached result. The example extension is guarded by
`NBNT_BUILD_EXAMPLES` (default `OFF`), so consuming projects do not build
it.
