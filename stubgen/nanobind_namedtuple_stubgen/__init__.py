"""Stubgen extension for nanobind_namedtuple.

Provides :class:`NamedTupleStubGen`, a subclass of :class:`nanobind.stubgen.StubGen`
that recognises classes registered through ``nbnt::bind_namedtuple<T>`` and emits
canonical ``typing.NamedTuple`` stubs instead of nanobind's default ``class X(tuple)``
rendering.

Registered classes carry two sentinel attributes attached at bind time:

* ``__nb_named_tuple__`` — always ``True``.
* ``__nb_nt_annotations__`` — an ordered mapping from field name to a Python-syntax
  annotation string (e.g. ``"float"``, ``"str"``, or ``"typing.Any"`` for opaque
  fields whose caster carries type-substitution slots).

Default values come from :attr:`_field_defaults`, inherited from
``collections.namedtuple``. Docstrings set at bind time (class ``__doc__`` and
per-field property ``__doc__``) are emitted into the stub; the synthetic
docstrings ``collections.namedtuple`` generates on its own are suppressed.
"""

from __future__ import annotations

import collections
import re
from typing import Any

from nanobind.stubgen import StubGen

__all__ = ["NamedTupleStubGen", "is_namedtuple_class"]

# Dotted names inside an annotation string, e.g. ``typing.Optional`` in
# ``typing.Optional[float]``; subscripts and punctuation are left intact.
_DOTTED_NAME_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)+")


def is_namedtuple_class(tp: Any) -> bool:
    """Return ``True`` if ``tp`` was registered through ``bind_namedtuple``."""
    return isinstance(tp, type) and bool(getattr(tp, "__nb_named_tuple__", False))


class NamedTupleStubGen(StubGen):
    """StubGen variant that emits ``typing.NamedTuple`` blocks for registered types."""

    def put_type(self, tp: Any, name: str | None) -> None:
        if not is_namedtuple_class(tp):
            super().put_type(tp, name)
            return

        tp_name = tp.__name__
        tp_mod_name = tp.__module__
        mod_name = self.module.__name__

        # Aliases and cross-module references fall through to nanobind's default
        # emitter, matching how it handles other types with mismatched name/module.
        if name and (name != tp_name or mod_name != tp_mod_name):
            super().put_type(tp, name)
            return

        annotations = getattr(tp, "__nb_nt_annotations__", None) or {}
        field_names = getattr(tp, "_fields", ())
        field_defaults = getattr(tp, "_field_defaults", {})

        nt_ref = self.import_object("typing", "NamedTuple")
        self.write_ln(f"class {tp_name}({nt_ref}):")
        self.depth += 1

        # Reference namedtuple whose synthetic docstrings ("Point(x, y)",
        # "Alias for field number 0") identify docs not set at bind time.
        reference = collections.namedtuple(tp_name, field_names)

        docstr = tp.__doc__
        if docstr and docstr != reference.__doc__ and self.include_docstrings:
            self.put_docstr(docstr)
            if field_names:
                self.write("\n")

        if not field_names:
            self.write_ln("pass\n")
            self.depth -= 1
            return

        for fname in field_names:
            annot = annotations.get(fname, "typing.Any")
            annot_str = self._rewrite_annotation(annot)
            if fname in field_defaults:
                default_expr = self.expr_str(field_defaults[fname], abbrev=False) or "..."
                self.write_ln(f"{fname}: {annot_str} = {default_expr}")
            else:
                self.write_ln(f"{fname}: {annot_str}")
            if self.include_docstrings:
                fdoc = getattr(getattr(tp, fname, None), "__doc__", None)
                if fdoc and fdoc != getattr(reference, fname).__doc__:
                    self.put_docstr(fdoc)
        self.write("\n")
        self.depth -= 1

    def _rewrite_annotation(self, annot: str) -> str:
        """Route each dotted name through :meth:`import_object` for import bookkeeping."""
        if not annot:
            return "typing.Any"

        def rewrite(match: re.Match[str]) -> str:
            module_name, _, leaf = match.group().rpartition(".")
            return self.import_object(module_name, leaf)

        return _DOTTED_NAME_RE.sub(rewrite, annot)
