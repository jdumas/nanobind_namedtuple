"""Concurrent ``from_cpp`` smoke test for free-threaded Python builds.

On a GIL build the loop still runs — CPython schedules the threads
cooperatively — but the interesting assertion is on free-threaded 3.13t /
3.14t interpreters, where N threads execute the caster in parallel with no
lock protecting ``nt_class<T>::cls``. The test guards against a data race on
the publication slot regressing under the relaxed hot-path load.
"""

from __future__ import annotations

import sys
import threading

import pytest
from nanobind_namedtuple_examples import nbnt_example_hello

is_free_threaded = getattr(sys, "_is_gil_enabled", lambda: True)() is False


def _make_many(count: int, out: list) -> None:
    local: list = []
    for i in range(count):
        local.append(nbnt_example_hello.make_color(float(i), float(i + 1), float(i + 2)))
    out.extend(local)


def _spawn_and_join(threads):
    for t in threads:
        t.start()
    for t in threads:
        t.join()


def test_concurrent_from_cpp_smoke():
    per_thread = 500
    num_threads = 8
    results: list[list] = [[] for _ in range(num_threads)]
    threads = [
        threading.Thread(target=_make_many, args=(per_thread, results[i]))
        for i in range(num_threads)
    ]
    _spawn_and_join(threads)

    for bucket in results:
        assert len(bucket) == per_thread
        for value in bucket:
            assert type(value) is nbnt_example_hello.Color
            assert isinstance(value, tuple)
            assert len(value) == 3


@pytest.mark.skipif(
    not is_free_threaded,
    reason="race window only observable on free-threaded interpreters",
)
def test_concurrent_from_cpp_on_free_threaded_build():
    per_thread = 2000
    num_threads = 16
    results: list[list] = [[] for _ in range(num_threads)]
    threads = [
        threading.Thread(target=_make_many, args=(per_thread, results[i]))
        for i in range(num_threads)
    ]
    _spawn_and_join(threads)

    total = sum(len(bucket) for bucket in results)
    assert total == per_thread * num_threads
    identity_seen = set()
    for bucket in results:
        for value in bucket:
            identity_seen.add(type(value))
    assert identity_seen == {nbnt_example_hello.Color}
