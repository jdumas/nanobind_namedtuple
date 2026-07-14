#include <string>

#include <nanobind/nanobind.h>
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

NB_NAMED_TUPLE(Color, "Color", NB_NT_FIELD(r), NB_NT_FIELD(g), NB_NT_FIELD(b))

NB_NAMED_TUPLE_EX(
    Point, "Point", nbnt::field<&Point::x>("x"), nbnt::field<&Point::y>("y"),
    nbnt::field<&Point::label>("label").default_(std::string{})
)

NB_NAMED_TUPLE(Empty, "Empty")

NB_MODULE(nbnt_example_hello, m) {
    m.doc() = "Minimal nanobind extension used by the nanobind_namedtuple test suite.";
    m.def("hello", []() { return "hello from nanobind_namedtuple"; });
    m.def("add", [](int a, int b) { return a + b; });

    nbnt::bind_namedtuple<Color>(m);
    nbnt::bind_namedtuple<Point>(m);
    nbnt::bind_namedtuple<Empty>(m);

    m.def("rebind_color", [](nb::module_ mod) { nbnt::bind_namedtuple<Color>(mod); });

    m.def("make_color", [](float r, float g, float b) { return Color{r, g, b}; });
    m.def("sum_color", [](Color c) { return c.r + c.g + c.b; });

    m.def("make_point", [](int x, int y, std::string label) {
        return Point{x, y, std::move(label)};
    });
    m.def("point_label", [](Point p) { return p.label; });

    m.def("make_empty", []() { return Empty{}; });
    m.def("take_empty", [](Empty) { return true; });
}
