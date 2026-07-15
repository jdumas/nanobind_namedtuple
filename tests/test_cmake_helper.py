"""Exercise the ``nanobind_namedtuple_stub_pattern()`` CMake helper end to end.

A throwaway language-free CMake project includes the helper module from the
repository checkout and calls the helper against the installed example
extension — build-time mode (add_custom_command) and install-time mode
(install(CODE)). Each generated pattern file is then consumed by vanilla
``python -m nanobind.stubgen`` and the resulting stub must contain canonical
``typing.NamedTuple`` blocks with no ``collections._tuplegetter`` leftovers.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent
HELPER_MODULE = REPO_ROOT / "cmake" / "nanobind_namedtuple.cmake"
MODULE_NAME = "nanobind_namedtuple_examples.nbnt_example_hello"

pytestmark = pytest.mark.skipif(
    shutil.which("cmake") is None or not HELPER_MODULE.exists(),
    reason="requires cmake on PATH and a repository checkout",
)


def _write_project(project_dir: Path, extra_args: str = "") -> None:
    project_dir.mkdir()
    (project_dir / "CMakeLists.txt").write_text(
        f"""
cmake_minimum_required(VERSION 3.15)
project(pattern_helper_test NONE)
include("{HELPER_MODULE.as_posix()}")
nanobind_namedtuple_stub_pattern(
    OUTPUT hello.pat
    MODULE {MODULE_NAME}
    {extra_args})
""",
        encoding="utf-8",
    )


def _configure(project_dir: Path) -> Path:
    build_dir = project_dir / "build"
    subprocess.run(
        [
            "cmake",
            "-S",
            str(project_dir),
            "-B",
            str(build_dir),
            f"-DPython_EXECUTABLE={Path(sys.executable).as_posix()}",
        ],
        check=True,
    )
    return build_dir


def _assert_pattern_produces_namedtuple_stub(pattern_file: Path, stub_file: Path) -> None:
    text = pattern_file.read_text(encoding="utf-8")
    assert "class Color(NamedTuple):" in text
    subprocess.run(
        [
            sys.executable,
            "-m",
            "nanobind.stubgen",
            "-m",
            MODULE_NAME,
            "-p",
            str(pattern_file),
            "-o",
            str(stub_file),
            "-q",
        ],
        check=True,
    )
    stub = stub_file.read_text(encoding="utf-8")
    assert "from typing import NamedTuple" in stub
    assert "class Color(NamedTuple):" in stub
    assert "_tuplegetter" not in stub


def test_build_time_pattern_generation(tmp_path):
    project_dir = tmp_path / "proj"
    _write_project(project_dir)
    build_dir = _configure(project_dir)
    subprocess.run(["cmake", "--build", str(build_dir)], check=True)
    pattern_file = build_dir / "hello.pat"
    assert pattern_file.exists()
    _assert_pattern_produces_namedtuple_stub(pattern_file, tmp_path / "hello.pyi")


def test_install_time_pattern_generation(tmp_path):
    project_dir = tmp_path / "proj"
    _write_project(project_dir, "INSTALL_TIME")
    build_dir = _configure(project_dir)
    prefix = tmp_path / "stage"
    subprocess.run(["cmake", "--install", str(build_dir), "--prefix", str(prefix)], check=True)
    pattern_file = prefix / "hello.pat"
    assert pattern_file.exists()
    _assert_pattern_produces_namedtuple_stub(pattern_file, tmp_path / "hello.pyi")
