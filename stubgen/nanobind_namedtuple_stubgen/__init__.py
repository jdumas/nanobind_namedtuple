"""Pattern-file generator for nanobind_namedtuple.

Classes registered through ``nbnt::bind_namedtuple<T>`` carry two sentinel
attributes attached at bind time:

* ``__nb_named_tuple__`` — always ``True``.
* ``__nb_nt_annotations__`` — an ordered mapping from field name to a Python-syntax
  annotation string (e.g. ``"float"``, ``"str"``, or ``"typing.Any"`` for opaque
  fields whose caster carries type-substitution slots).

This package turns those sentinels into a nanobind stubgen *pattern file*
(nanobind docs: "Typing → Pattern files"): one entry per registered class,
whose replacement is the canonical ``typing.NamedTuple`` block. Vanilla
``python -m nanobind.stubgen -p <pattern file>`` then substitutes that block
for its default ``class X(tuple)`` rendering.

Default values come from :attr:`_field_defaults`, inherited from
``collections.namedtuple``. Docstrings set at bind time (class ``__doc__`` and
per-field property ``__doc__``) are emitted into the replacement block; the
synthetic docstrings ``collections.namedtuple`` generates on its own are
suppressed.
"""

from __future__ import annotations

import collections
import re
import textwrap
from collections.abc import Iterable
from types import ModuleType
from typing import Any

__all__ = [
    "find_namedtuple_classes",
    "generate_pattern_file",
    "is_namedtuple_class",
    "pattern_entry",
]

# Dotted names inside an annotation string, e.g. ``typing.Optional`` in
# ``typing.Optional[float]``; subscripts and punctuation are left intact.
_DOTTED_NAME_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)+")

_INDENT = "    "


def is_namedtuple_class(tp: Any) -> bool:
    """Return ``True`` if ``tp`` was registered through ``bind_namedtuple``."""
    return isinstance(tp, type) and bool(getattr(tp, "__nb_named_tuple__", False))


def find_namedtuple_classes(module: ModuleType) -> list[type]:
    """Registered namedtuple classes defined in ``module``, in registration order.

    Aliases and re-exports are skipped: a class only counts when both its
    ``__name__`` matches the attribute name and its ``__module__`` matches
    the module, mirroring how nanobind's stubgen treats mismatches.
    """
    classes = []
    for name, value in vars(module).items():
        if (
            is_namedtuple_class(value)
            and value.__name__ == name
            and value.__module__ == module.__name__
        ):
            classes.append(value)
    return classes


def _docstr_lines(docstr: str) -> list[str]:
    """Format a docstring exactly like nanobind's ``StubGen.put_docstr``."""
    docstr = textwrap.dedent(docstr).strip()
    raw_str = ""
    if "''" in docstr or "\\" in docstr:
        # Escape all double quotes so that no unquoted triple quote can exist
        docstr = docstr.replace("''", "\\'\\'")
        raw_str = "r"
    if len(docstr) > 70 or "\n" in docstr:
        docstr = "\n" + docstr + "\n"
    return f'{raw_str}"""{docstr}"""'.split("\n")


def _default_expr(value: Any) -> str:
    """Python-syntax expression for a field default; ``...`` when unrepresentable."""
    if isinstance(value, float):
        s = repr(value)
        return f"float('{s}')" if ("inf" in s or "nan" in s) else s
    if value is None or value is Ellipsis or isinstance(value, (bool, int, str, bytes)):
        return repr(value)
    if isinstance(value, (tuple, list)):
        items = ", ".join(_default_expr(v) for v in value)
        if isinstance(value, list):
            return f"[{items}]"
        return f"({items},)" if len(value) == 1 else f"({items})"
    return "..."


def pattern_entry(cls: type) -> str:
    """Pattern-file entry for one registered class.

    The query is an anchored, dot-escaped regex on the fully-qualified class
    name; the replacement is the canonical ``typing.NamedTuple`` block,
    preceded by ``\\from`` / ``\\import`` escape lines so the imports of the
    final stub resolve.
    """
    name = cls.__name__
    annotations = getattr(cls, "__nb_nt_annotations__", None) or {}
    field_names = getattr(cls, "_fields", ())
    field_defaults = getattr(cls, "_field_defaults", {})

    body = ["\\from typing import NamedTuple"]
    referenced = {
        m.group().rpartition(".")[0]
        for annot in annotations.values()
        for m in _DOTTED_NAME_RE.finditer(annot)
    }
    for mod in sorted(referenced):
        body.append(f"\\import {mod}")
    body.append(f"class {name}(NamedTuple):")

    # Reference namedtuple whose synthetic docstrings ("Point(x, y)",
    # "Alias for field number 0") identify docs not set at bind time.
    reference = collections.namedtuple(name, field_names)

    block: list[str] = []
    docstr = cls.__doc__
    if docstr and docstr != reference.__doc__:
        block.extend(_docstr_lines(docstr))
        if field_names:
            block.append("")

    for fname in field_names:
        annot = annotations.get(fname, "typing.Any")
        if fname in field_defaults:
            block.append(f"{fname}: {annot} = {_default_expr(field_defaults[fname])}")
        else:
            block.append(f"{fname}: {annot}")
        fdoc = getattr(getattr(cls, fname, None), "__doc__", None)
        if fdoc and fdoc != getattr(reference, fname).__doc__:
            block.extend(_docstr_lines(fdoc))

    if not field_names:
        block.append("pass")

    body.extend(_INDENT + line if line else "" for line in block)
    lines = ["^" + re.escape(f"{cls.__module__}.{name}") + "$:"]
    lines.extend(_INDENT + line if line else "" for line in body)
    return "\n".join(lines) + "\n"


def generate_pattern_file(modules: ModuleType | Iterable[ModuleType]) -> str:
    """Pattern-file text covering every registered class found in ``modules``."""
    if isinstance(modules, ModuleType):
        modules = [modules]
    entries = [pattern_entry(cls) for module in modules for cls in find_namedtuple_classes(module)]
    return "\n".join(entries)
