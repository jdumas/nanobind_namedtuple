"""pytest collection configuration.

``test_match_args_pattern.py`` uses ``match``/``case`` (PEP 634), which is
a parse-time syntax error on Python 3.9. Exclude that file from collection
there so the rest of the suite still runs; on 3.10+ it is collected and
executed normally.
"""

from __future__ import annotations

import sys

collect_ignore: list[str] = []

if sys.version_info < (3, 10):
    collect_ignore.append("test_match_args_pattern.py")
