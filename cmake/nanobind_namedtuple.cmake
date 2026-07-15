# Defines nanobind_namedtuple_stub_pattern(), which generates the nanobind
# stubgen pattern file for classes registered through nbnt::bind_namedtuple<T>.
# Consumers pass the OUTPUT file to an untouched nanobind_add_stub() via its
# PATTERN_FILE argument. Argument names mirror nanobind_add_stub():
#
#   nanobind_namedtuple_stub_pattern(
#       OUTPUT <file>            # pattern file to generate (required)
#       MODULE <mod> [<mod>...]  # modules to scan (required)
#       [PYTHON_PATH <path>...]  # extra import-path entries
#       [DEPENDS <target>...]    # build-time dependencies
#       [RECURSIVE]              # recurse into submodules
#       [INSTALL_TIME]           # generate via install(CODE); declare before
#                                # the matching INSTALL_TIME nanobind_add_stub()
#       [COMPONENT <comp>]       # install component (INSTALL_TIME only)
#       [EXCLUDE_FROM_ALL]       # exclude from default installation
#       [VERBOSE]                # show generator output during the build
#                                # (build-time mode only; install(CODE) rules
#                                # always stream their output)
#   )

# Captured at include time (CMAKE_CURRENT_FUNCTION_LIST_DIR needs CMake 3.17;
# floor is 3.15); INTERNAL cache keeps it visible from any consumer scope.
get_filename_component(NBNT_STUBGEN_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../stubgen" ABSOLUTE)
set(NBNT_STUBGEN_SOURCE_DIR
    "${NBNT_STUBGEN_SOURCE_DIR}"
    CACHE INTERNAL "Directory containing the nanobind_namedtuple_stubgen package")

function(nanobind_namedtuple_stub_pattern)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "INSTALL_TIME;RECURSIVE;VERBOSE;EXCLUDE_FROM_ALL"
                          "OUTPUT;COMPONENT" "MODULE;PYTHON_PATH;DEPENDS")

    if(NOT ARG_OUTPUT)
        message(
            FATAL_ERROR
                "nanobind_namedtuple_stub_pattern(): an 'OUTPUT' argument must be specified!")
    endif()
    if(NOT ARG_MODULE)
        message(
            FATAL_ERROR "nanobind_namedtuple_stub_pattern(): a 'MODULE' argument must be specified!"
        )
    endif()

    set(NBNT_STUBGEN_ARGS -o "${ARG_OUTPUT}")
    foreach(MODULE IN LISTS ARG_MODULE)
        list(APPEND NBNT_STUBGEN_ARGS -m "${MODULE}")
    endforeach()
    foreach(PYTHON_PATH IN LISTS ARG_PYTHON_PATH)
        list(APPEND NBNT_STUBGEN_ARGS -i "${PYTHON_PATH}")
    endforeach()
    if(ARG_RECURSIVE)
        list(APPEND NBNT_STUBGEN_ARGS -r)
    endif()

    # The generator package ships in this repository's stubgen/ directory;
    # expose it by prepending it to any ambient (configure-time) PYTHONPATH.
    set(NBNT_PYTHONPATH "${NBNT_STUBGEN_SOURCE_DIR}")
    if(DEFINED ENV{PYTHONPATH})
        if(CMAKE_HOST_WIN32)
            string(APPEND NBNT_PYTHONPATH ";$ENV{PYTHONPATH}")
        else()
            string(APPEND NBNT_PYTHONPATH ":$ENV{PYTHONPATH}")
        endif()
    endif()
    file(TO_CMAKE_PATH "${Python_EXECUTABLE}" NBNT_PYTHON_EXECUTABLE)
    set(NBNT_STUBGEN_CMD "${NBNT_PYTHON_EXECUTABLE}" -m nanobind_namedtuple_stubgen
                         ${NBNT_STUBGEN_ARGS})

    if(NOT ARG_INSTALL_TIME)
        if(ARG_VERBOSE)
            set(NBNT_STUBGEN_EXTRA USES_TERMINAL)
        endif()
        # PYTHONPATH stays a single quoted argument so a Windows ";" list
        # separator inside its value cannot split the command.
        add_custom_command(
            OUTPUT "${ARG_OUTPUT}"
            COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${NBNT_PYTHONPATH}" ${NBNT_STUBGEN_CMD}
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            DEPENDS ${ARG_DEPENDS} ${NBNT_STUBGEN_EXTRA})
        # Driving target named after the output file (e.g. my_ext.pat ->
        # my_ext_pat), so the pattern file is generated as part of "all".
        get_filename_component(NBNT_OUTPUT_NAME "${ARG_OUTPUT}" NAME)
        string(MAKE_C_IDENTIFIER "${NBNT_OUTPUT_NAME}" NBNT_PATTERN_TARGET)
        add_custom_target(${NBNT_PATTERN_TARGET} ALL DEPENDS "${ARG_OUTPUT}")
    else()
        set(NBNT_INSTALL_EXTRA "")
        if(ARG_COMPONENT)
            list(APPEND NBNT_INSTALL_EXTRA COMPONENT ${ARG_COMPONENT})
        endif()
        if(ARG_EXCLUDE_FROM_ALL)
            list(APPEND NBNT_INSTALL_EXTRA EXCLUDE_FROM_ALL)
        endif()
        # Working directory matches nanobind_add_stub(INSTALL_TIME); the quoted
        # PYTHONPATH keeps a Windows ";" separator intact in the install script.
        install(
            CODE "set(NBNT_CMD \"${NBNT_STUBGEN_CMD}\")
file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}\")
execute_process(
    COMMAND \"${CMAKE_COMMAND}\" -E env \"PYTHONPATH=${NBNT_PYTHONPATH}\" \${NBNT_CMD}
    WORKING_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}\"
    COMMAND_ERROR_IS_FATAL ANY)"
            ${NBNT_INSTALL_EXTRA})
    endif()
endfunction()
