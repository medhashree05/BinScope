# ---------------------------------------------------------------------------
# Fixtures: small C++ programs compiled during the build so no prebuilt
# binaries are committed. They are compiled with the plain compiler (never with
# sanitizer flags) because they are inputs to analyze, not code under test.
# ---------------------------------------------------------------------------
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "BinScope fixtures require a Linux toolchain that emits ELF64; tests are disabled.")
    return()
endif()

set(FIXTURE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
set(FIXTURE_OUT ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
file(MAKE_DIRECTORY ${FIXTURE_OUT})

set(FIXTURE_CXXFLAGS -std=c++17 -O0)

# Compile one fixture translation unit to an object file.
function(binscope_fixture_object name)
    cmake_parse_arguments(ARG "" "SOURCE" "FLAGS" ${ARGN})
    add_custom_command(
        OUTPUT ${FIXTURE_OUT}/${name}
        COMMAND ${CMAKE_CXX_COMPILER} ${FIXTURE_CXXFLAGS} ${ARG_FLAGS}
                -c ${FIXTURE_SRC}/${ARG_SOURCE} -o ${FIXTURE_OUT}/${name}
        DEPENDS ${FIXTURE_SRC}/${ARG_SOURCE}
        COMMENT "Compiling fixture ${name}"
        VERBATIM)
endfunction()

binscope_fixture_object(simple.o SOURCE simple.cpp)
binscope_fixture_object(simple_O2.o SOURCE simple.cpp FLAGS -O2)
binscope_fixture_object(helper.o SOURCE helper.cpp FLAGS -fPIE)
binscope_fixture_object(main.o SOURCE main.cpp FLAGS -fPIE)
binscope_fixture_object(data_only.o SOURCE data_only.cpp)

# Position-independent executable (the Ubuntu default) -> ET_DYN with INTERP.
add_custom_command(
    OUTPUT ${FIXTURE_OUT}/program
    COMMAND ${CMAKE_CXX_COMPILER} -pie ${FIXTURE_OUT}/main.o ${FIXTURE_OUT}/helper.o
            -o ${FIXTURE_OUT}/program
    DEPENDS ${FIXTURE_OUT}/main.o ${FIXTURE_OUT}/helper.o
    COMMENT "Linking fixture program (PIE)"
    VERBATIM)

# Non-PIE executable -> ET_EXEC with fixed load addresses.
add_custom_command(
    OUTPUT ${FIXTURE_OUT}/program_nopie
    COMMAND ${CMAKE_CXX_COMPILER} -no-pie ${FIXTURE_OUT}/main.o ${FIXTURE_OUT}/helper.o
            -o ${FIXTURE_OUT}/program_nopie
    DEPENDS ${FIXTURE_OUT}/main.o ${FIXTURE_OUT}/helper.o
    COMMENT "Linking fixture program_nopie (ET_EXEC)"
    VERBATIM)

# Shared object -> ET_DYN without an interpreter.
add_custom_command(
    OUTPUT ${FIXTURE_OUT}/libhelper.so
    COMMAND ${CMAKE_CXX_COMPILER} ${FIXTURE_CXXFLAGS} -fPIC -shared
            ${FIXTURE_SRC}/helper.cpp -o ${FIXTURE_OUT}/libhelper.so
    DEPENDS ${FIXTURE_SRC}/helper.cpp
    COMMENT "Building fixture libhelper.so"
    VERBATIM)

set(FIXTURE_FILES
    ${FIXTURE_OUT}/simple.o
    ${FIXTURE_OUT}/simple_O2.o
    ${FIXTURE_OUT}/helper.o
    ${FIXTURE_OUT}/main.o
    ${FIXTURE_OUT}/data_only.o
    ${FIXTURE_OUT}/program
    ${FIXTURE_OUT}/program_nopie
    ${FIXTURE_OUT}/libhelper.so)

# Objects without a symbol table, produced with binutils strip when available.
if(CMAKE_STRIP)
    add_custom_command(
        OUTPUT ${FIXTURE_OUT}/nosym.o
        COMMAND ${CMAKE_STRIP} --strip-all ${FIXTURE_OUT}/data_only.o -o ${FIXTURE_OUT}/nosym.o
        DEPENDS ${FIXTURE_OUT}/data_only.o
        COMMENT "Stripping fixture nosym.o"
        VERBATIM)
    add_custom_command(
        OUTPUT ${FIXTURE_OUT}/program_stripped
        COMMAND ${CMAKE_STRIP} --strip-all ${FIXTURE_OUT}/program -o ${FIXTURE_OUT}/program_stripped
        DEPENDS ${FIXTURE_OUT}/program
        COMMENT "Stripping fixture program_stripped"
        VERBATIM)
    list(APPEND FIXTURE_FILES ${FIXTURE_OUT}/nosym.o ${FIXTURE_OUT}/program_stripped)
    set(BINSCOPE_HAVE_STRIP 1)
else()
    message(WARNING "strip not found; tests that need a symbol-less object are skipped.")
    set(BINSCOPE_HAVE_STRIP 0)
endif()

add_custom_target(binscope_fixtures ALL DEPENDS ${FIXTURE_FILES})

# ---------------------------------------------------------------------------
# Unit tests: one executable per area, each registered with CTest.
# ---------------------------------------------------------------------------
function(binscope_add_test name)
    add_executable(${name} ${name}.cpp test_main.cpp)
    target_link_libraries(${name} PRIVATE binscope_core)
    target_compile_definitions(${name} PRIVATE
        BINSCOPE_FIXTURE_DIR="${FIXTURE_OUT}"
        BINSCOPE_HAVE_STRIP=${BINSCOPE_HAVE_STRIP})
    add_dependencies(${name} binscope_fixtures)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

binscope_add_test(test_parser)
binscope_add_test(test_sections)
binscope_add_test(test_symbols)
binscope_add_test(test_relocations)
binscope_add_test(test_compare)
binscope_add_test(test_malformed_files)
binscope_add_test(test_cli)
binscope_add_test(test_constants)

# Independent oracle: compare parsed structures with GNU readelf when present.
find_program(BINSCOPE_READELF_EXECUTABLE NAMES readelf)
binscope_add_test(test_readelf_crosscheck)
if(BINSCOPE_READELF_EXECUTABLE)
    target_compile_definitions(test_readelf_crosscheck PRIVATE
        BINSCOPE_READELF="${BINSCOPE_READELF_EXECUTABLE}")
else()
    message(WARNING "readelf not found; the readelf cross-check test will be skipped.")
endif()

# ---------------------------------------------------------------------------
# End-to-end checks of the real executable's exit codes and streams.
# ---------------------------------------------------------------------------
add_test(NAME cli_help COMMAND binscope --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:")

add_test(NAME cli_no_arguments COMMAND binscope)
set_tests_properties(cli_no_arguments PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_inspect_object COMMAND binscope inspect ${FIXTURE_OUT}/simple.o)
set_tests_properties(cli_inspect_object PROPERTIES
    PASS_REGULAR_EXPRESSION "REL \\(relocatable object file\\)")

add_test(NAME cli_inspect_missing_file COMMAND binscope inspect ${FIXTURE_OUT}/does-not-exist.o)
set_tests_properties(cli_inspect_missing_file PROPERTIES WILL_FAIL TRUE)
