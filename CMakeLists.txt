cmake_minimum_required(VERSION 3.16)

project(BinScope
    VERSION 0.1.0
    DESCRIPTION "ELF binary and linker analysis toolkit"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(BINSCOPE_BUILD_TESTS "Build the BinScope test suite" ON)
option(BINSCOPE_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(BINSCOPE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Shared compile settings (warnings, optional sanitizers)
# ---------------------------------------------------------------------------
add_library(binscope_options INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(binscope_options INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual
        -Wnull-dereference -Wformat=2)
    if(BINSCOPE_WARNINGS_AS_ERRORS)
        target_compile_options(binscope_options INTERFACE -Werror)
    endif()
    if(BINSCOPE_ENABLE_SANITIZERS)
        target_compile_options(binscope_options INTERFACE
            -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(binscope_options INTERFACE
            -fsanitize=address,undefined)
    endif()
elseif(MSVC)
    target_compile_options(binscope_options INTERFACE /W4 /permissive-)
    if(BINSCOPE_WARNINGS_AS_ERRORS)
        target_compile_options(binscope_options INTERFACE /WX)
    endif()
endif()

# ---------------------------------------------------------------------------
# Core library: parser, analysis and formatting (everything except main())
# ---------------------------------------------------------------------------
add_library(binscope_core STATIC
    src/byte_reader.cpp
    src/elf_header.cpp
    src/parser.cpp
    src/sections.cpp
    src/symbols.cpp
    src/relocations.cpp
    src/elf_file.cpp
    src/analyzer.cpp
    src/formatter.cpp
    src/cli.cpp)

target_include_directories(binscope_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(binscope_core PUBLIC binscope_options)

add_executable(binscope src/main.cpp)
target_link_libraries(binscope PRIVATE binscope_core)

include(GNUInstallDirs)
install(TARGETS binscope RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BINSCOPE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
