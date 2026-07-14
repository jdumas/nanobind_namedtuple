"""Reference stub for ``nbnt_example_hello``.

Regenerate with:

    python -m nanobind_namedtuple_stubgen -m nbnt_example_hello -o examples/hello.pyi

The ``typing.NamedTuple`` classes are produced by the stubgen extension shipped
in ``stubgen/nanobind_namedtuple_stubgen``; a plain ``python -m nanobind.stubgen``
run would emit ``class X(tuple): ...`` blocks instead.
"""

import types
from typing import NamedTuple

def hello() -> str: ...
def add(arg0: int, arg1: int, /) -> int: ...

class Color(NamedTuple):
    r: float
    g: float
    b: float

class Point(NamedTuple):
    x: int
    y: int
    label: str = ""

class Empty(NamedTuple):
    pass

class Vec3(NamedTuple):
    x: float
    y: float
    z: float

class Payload(NamedTuple):
    obj: object

def rebind_color(arg: types.ModuleType, /) -> None: ...
def make_color(arg0: float, arg1: float, arg2: float, /) -> Color: ...
def sum_color(arg: Color, /) -> float: ...
def make_point(arg0: int, arg1: int, arg2: str, /) -> Point: ...
def point_label(arg: Point, /) -> str: ...
def make_empty() -> Empty: ...
def take_empty(arg: Empty, /) -> bool: ...
def make_vec3(arg0: float, arg1: float, arg2: float, /) -> Vec3: ...
def sum_vec3(arg: Vec3, /) -> float: ...
def make_payload(arg: object, /) -> Payload: ...
def payload_obj(arg: Payload, /) -> object: ...
