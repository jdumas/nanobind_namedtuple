cmake_minimum_required(VERSION 3.15...3.30)
project(nanobind_namedtuple LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NBNT_BUILD_EXAMPLES "Build the example nanobind extension" OFF)

# Locate the active Python interpreter and nanobind's CMake package.
find_package(
    Python 3.9
    COMPONENTS Interpreter Development.Module
    REQUIRED)

execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE NB_DIR)
list(APPEND CMAKE_PREFIX_PATH "${NB_DIR}")
find_package(nanobind CONFIG REQUIRED)

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
