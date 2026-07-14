#include <optional>
#include <string>

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>

#include <nanobind_namedtuple/named_tuple.h>

namespace nb = nanobind;

struct Color {
    float r;
    float g;
    float b;
};

struct Point {
    int x;
    int y;
    std::string label;
};

struct Empty {};

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Payload {
    nb::object obj;
};

// Nested record whose field types are themselves ``NB_NAMED_TUPLE``-bound
// classes. Exercises the recursive ``make_caster`` field-conversion path.
struct Pixel {
    Vec3 position;
    Color color;
};

// Record with std::optional<int>, exercising nanobind/stl/optional.h
// (None <-> std::nullopt, integers <-> engaged value).
struct Tagged {
    int value;
    std::optional<int> tag;
};

NB_NAMED_TUPLE(Color, "Color", NB_NT_FIELD(r), NB_NT_FIELD(g), NB_NT_FIELD(b))

NB_NAMED_TUPLE_EX(
    Point, "Point", nbnt::field<&Point::x>("x"), nbnt::field<&Point::y>("y"),
    nbnt::field<&Point::label>("label").default_(std::string{})
)

NB_NAMED_TUPLE(Empty, "Empty")

NB_NAMED_TUPLE(Vec3, "Vec3", NB_NT_FIELD(x), NB_NT_FIELD(y), NB_NT_FIELD(z))

NB_NAMED_TUPLE(Payload, "Payload", NB_NT_FIELD(obj))

NB_NAMED_TUPLE(Pixel, "Pixel", NB_NT_FIELD(position), NB_NT_FIELD(color))

NB_NAMED_TUPLE_EX(
    Tagged, "Tagged", nbnt::field<&Tagged::value>("value"),
    nbnt::field<&Tagged::tag>("tag").default_(std::optional<int>{})
)

NB_MODULE(nbnt_example_hello, m) {
    m.doc() = "Minimal nanobind extension used by the nanobind_namedtuple test suite.";
    m.def("hello", []() { return "hello from nanobind_namedtuple"; });
    m.def("add", [](int a, int b) { return a + b; });

    nbnt::bind_namedtuple<Color>(m);
    nbnt::bind_namedtuple<Point>(m);
    nbnt::bind_namedtuple<Empty>(m);
    nbnt::bind_namedtuple<Vec3>(m);
    nbnt::bind_namedtuple<Payload>(m);
    nbnt::bind_namedtuple<Pixel>(m);
    nbnt::bind_namedtuple<Tagged>(m);

    m.def("rebind_color", [](nb::module_ mod) { nbnt::bind_namedtuple<Color>(mod); });

    m.def("make_color", [](float r, float g, float b) { return Color{r, g, b}; });
    m.def("sum_color", [](Color c) { return c.r + c.g + c.b; });

    m.def("make_point", [](int x, int y, std::string label) {
        return Point{x, y, std::move(label)};
    });
    m.def("point_label", [](Point p) { return p.label; });

    m.def("make_empty", []() { return Empty{}; });
    m.def("take_empty", [](Empty) { return true; });

    m.def("make_vec3", [](float x, float y, float z) { return Vec3{x, y, z}; });
    m.def("sum_vec3", [](Vec3 v) { return v.x + v.y + v.z; });

    m.def("make_payload", [](nb::object o) { return Payload{std::move(o)}; });
    m.def("payload_obj", [](Payload p) { return p.obj; });

    m.def("make_pixel", [](Vec3 position, Color color) { return Pixel{position, color}; });
    m.def("pixel_color", [](Pixel p) { return p.color; });
    m.def("pixel_position", [](Pixel p) { return p.position; });

    m.def("make_tagged", [](int value, std::optional<int> tag) { return Tagged{value, tag}; });
    m.def("tagged_tag", [](Tagged t) { return t.tag; });

    // rv_policy fixtures: reference/automatic_reference downgrade to copy
    // (move for rvalue sources); reference_internal/take_ownership raise TypeError.
    m.def(
        "make_color_reference", [](float r, float g, float b) { return Color{r, g, b}; },
        nb::rv_policy::reference
    );
    m.def(
        "make_color_reference_internal", [](float r, float g, float b) { return Color{r, g, b}; },
        nb::rv_policy::reference_internal
    );
    m.def(
        "make_color_take_ownership", [](float r, float g, float b) { return Color{r, g, b}; },
        nb::rv_policy::take_ownership
    );
    m.def(
        "make_color_copy", [](float r, float g, float b) { return Color{r, g, b}; },
        nb::rv_policy::copy
    );
    m.def(
        "make_color_move", [](float r, float g, float b) { return Color{r, g, b}; },
        nb::rv_policy::move
    );
}
