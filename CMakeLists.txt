# Copyright 2022 Nokia
# Copyright 2026 Shi-Zhong Chen
# Licensed under the BSD 3 Clause license
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION  3.16)

# ------------------------------------------------------------------------------------------------
project(ut_exe  CXX)  # as early as possible, eg must before CMAKE_CXX_COMPILER_ID

set(CMAKE_CXX_EXTENSIONS  OFF)

# ------------------------------------------------------------------------------------------------
# build mode: cmake -Dci=<mode> ..   (empty = fast UT, the local default)
# - 1 knob replaces CMAKE_BUILD_TYPE: each mode bundles -O/-g, sanitizer/coverage and macros
set(ci  ""  CACHE STRING  "UT build mode: <empty>=fast UT | smart | cov | asan | tsan | release")
set(_ci_allowed  ""  smart  cov  asan  tsan  release)
set_property(CACHE ci  PROPERTY STRINGS  "${_ci_allowed}")  # quoted: keep the empty entry
if(NOT ci  IN_LIST  _ci_allowed)
    message(FATAL_ERROR  "unknown ci='${ci}', expect one of: '${_ci_allowed}'")
endif()
if(CMAKE_BUILD_TYPE)
    message(FATAL_ERROR  "this project selects flags via -Dci=<mode>, not CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'")
endif()
set(_cxx_std  cxx_std_17)  # 17 since "if constexpr()"

if(ci  STREQUAL  "cov")
    message(STATUS "ci=cov: coverage; valgrind needs -g -fno-omit-frame-pointer")
    add_compile_options(--coverage -fPIC -O0 -fprofile-abs-path -g)  # gcov + valgrind req
    add_link_options(--coverage)                                     # gcov req
    add_compile_definitions(SMART_LOG WITH_HID_LOG)  # more info to debug ci
    set(_cxx_std  cxx_std_20)  # try c++20/23 to see cov rate inc or not

elseif(ci  STREQUAL  "asan" OR ci  STREQUAL  "tsan")
    # shared: debug info + -O2. asan also stack-protects; tsan does not (unrelated to races)
    add_compile_options(-g -fno-omit-frame-pointer -O2)
    if(ci STREQUAL "asan")
        message(STATUS "ci=asan: asan+lsan+ubsan (use-after-free, double-free, out-of-bound, leak, undef)")
        add_compile_definitions(SMART_LOG)
        add_compile_options(-fstack-protector-strong)  # stack overflow; glibc already has __stack_chk_fail
        add_compile_options(-fsanitize=address,undefined)
        add_compile_options(-fno-sanitize-recover=undefined)  # UBSan stops; ASan already does
        add_link_options(-fsanitize=address,undefined)
    else()
        message(STATUS "ci=tsan: thread; UniCoutLog; halt_on_error is in UtSoak.cpp")
        add_compile_options(-fsanitize=thread)
        add_link_options(-fsanitize=thread)
    endif()

elseif(ci  STREQUAL  "release")
    # release-perf build: cold/hot path separation comes from compiler flags, not source macros
    # - O3                              : enables block reordering, inlining, vectorization
    # - freorder-blocks-and-partition   : split hot vs cold blocks into .text vs .text.unlikely
    # - freorder-functions              : place mutually-called funcs adjacent (better i-cache)
    # - ffunction-sections/fdata-s.     : 1 section per func, lets linker drop dead code & re-pack hot
    # - Wl,--gc-sections                : actually drop unused sections at link time
    # - NDEBUG                          : disable assert in hot path
    # NOTE: -flto is intentionally OFF here; with our toolchain it merges all blocks back into a
    #       single .text and defeats the section partitioning we want to verify.
    message(STATUS "ci=release: max perf, hot/cold sections separated by compiler; no ut/")
    add_compile_definitions(NDEBUG)
    add_compile_options(-O3)
    add_compile_options(-freorder-blocks-and-partition -freorder-functions)
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)

else()
    # "" = fast UT; "smart" = same plus SmartLog (build-smart)
    add_compile_options(-O1)  # fastest build+ut (1-cpp-change=5s, vs 45s of -g)
    add_compile_definitions(IN_SPECIAL_UT)  # benchmark etc
    if(ci STREQUAL "smart")
        message(STATUS "ci=smart: fast UT + SmartLog")
        add_compile_definitions(SMART_LOG)
    else()
        message(STATUS "ci=<empty>: fast UT, no cov, no sanitizer")
    endif()
endif()

# not release: soak/ut helpers. Must be before add_subdirectory(src); later targets do not see it.
if(NOT ci STREQUAL "release")
    add_compile_definitions(IN_ALL_UT)
endif()

#add_compile_options(-fno-exceptions)  # inc branch coverage

add_subdirectory(src)

if(NOT ci STREQUAL "release")
    add_executable(ut_exe)

    # googletest via FetchContent (always latest, shallow clone to min download)
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY  https://github.com/google/googletest.git
        GIT_TAG         main
        GIT_SHALLOW     TRUE
    )
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(ut)

    target_link_libraries(ut_exe  PRIVATE
        lib_domino
        lib_ut
        gtest_main
    )

    # make run
    add_custom_target(run
        COMMAND  ./ut_exe --gtest_brief=0
        DEPENDS  ut_exe
    )

    # make soak  (not CI; gtest flags in soak.sh)
    add_custom_target(soak
        COMMAND  bash ${CMAKE_SOURCE_DIR}/ut/soak.sh
        DEPENDS  ut_exe
        WORKING_DIRECTORY  ${CMAKE_BINARY_DIR}
        USES_TERMINAL
    )
endif()

# C++ standard on targets (decided per ci mode above)
target_compile_features(lib_domino  PUBLIC  ${_cxx_std})

# make cov  (ci=cov only: fresh .gcda, then gcovr; reports land in this build dir)
if(ci STREQUAL "cov")
    find_program(GCOVR gcovr HINTS "$ENV{HOME}/.local/bin")
    if(NOT GCOVR)
        message(FATAL_ERROR "gcovr not found (PATH or ~/.local/bin)")
    endif()
    add_custom_target(cov
        COMMAND find . -name "*.gcda" -delete
        COMMAND ./ut_exe --gtest_brief=1
        COMMAND ${GCOVR} -k --config ${CMAKE_SOURCE_DIR}/ut/gcovr/gcovr.cfg
        DEPENDS ut_exe
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        USES_TERMINAL
        VERBATIM
    )
endif()

# simplify "make help"
set(CMAKE_SKIP_PREPROCESSED_SOURCE_RULES  true)
set(CMAKE_SKIP_ASSEMBLY_SOURCE_RULES      true)
