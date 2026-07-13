#include <nanobind/nanobind.h>

#include <nanobind_namedtuple/named_tuple.h>

namespace nb = nanobind;

NB_MODULE(nbnt_example_hello, m) {
    m.doc() = "Minimal nanobind extension used by the nanobind_namedtuple test suite.";
    m.def("hello", []() { return "hello from nanobind_namedtuple"; });
    m.def("add", [](int a, int b) { return a + b; });
}
