cmake_minimum_required(VERSION 3.15...3.30)
project(nanobind_namedtuple LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NBNT_BUILD_EXAMPLES "Build the example nanobind extension" OFF)

# Locate the active Python interpreter, unless a parent build (superbuild)
# already provides the Python targets.
if(NOT TARGET Python::Module)
    find_package(
        Python 3.9
        COMPONENTS Interpreter Development.Module
        REQUIRED)
endif()

# Locate nanobind's CMake package, unless a parent build already provides it;
# the interpreter probe below requires a pip-installed nanobind.
if(NOT COMMAND nanobind_add_module
   AND NOT nanobind_FOUND
   AND NOT TARGET nanobind-static
   AND NOT TARGET nanobind)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        OUTPUT_VARIABLE NB_DIR)
    list(APPEND CMAKE_PREFIX_PATH "${NB_DIR}")
    find_package(nanobind CONFIG REQUIRED)
endif()

# Header-only public interface.
add_library(nanobind_namedtuple INTERFACE)
add_library(nanobind_namedtuple::nanobind_namedtuple ALIAS nanobind_namedtuple)
target_include_directories(
    nanobind_namedtuple INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                  $<INSTALL_INTERFACE:include>)
target_compile_features(nanobind_namedtuple INTERFACE cxx_std_17)

if(NBNT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
