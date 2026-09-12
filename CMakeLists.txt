cmake_minimum_required(VERSION  3.16)

# ------------------------------------------------------------------------------------------------
project(ut_exe  CXX)  # as early as possible, eg must before CMAKE_CXX_FLAGS

set(CMAKE_CXX_EXTENSIONS  OFF)

# ------------------------------------------------------------------------------------------------
# build mode: cmake -Dci=<mode> ..   (empty = fast UT, the local default)
# - 1 knob replaces CMAKE_BUILD_TYPE: each mode bundles -O/-g, sanitizer/coverage and macros
set(ci  ""  CACHE STRING  "UT build mode: <empty>=fast UT | cov | asan | tsan | release")
set(_ci_allowed  ""  cov  asan  tsan  release)
set_property(CACHE ci  PROPERTY STRINGS  ${_ci_allowed})
if(NOT ci  IN_LIST  _ci_allowed)
    message(FATAL_ERROR  "unknown ci='${ci}', expect one of: cov asan tsan release (or empty)")
endif()
if(CMAKE_BUILD_TYPE)
    message(FATAL_ERROR  "this project selects flags via -Dci=<mode>, not CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'")
endif()
set(_cxx_std  cxx_std_17)  # 17 since "if constexpr()"

if(ci  STREQUAL  "cov")
    message("!!! coverage & valgrind(only need -g -fno-omit-frame-pointer)")
    add_compile_options(--coverage -fPIC -O0 -fprofile-abs-path -g)  # gcov + valgrind req
    add_link_options(--coverage)                                     # gcov req
    add_compile_definitions(SMART_LOG WITH_HID_LOG)  # more info to debug ci
    set(_cxx_std  cxx_std_20)  # try c++20/23 to see cov rate inc or not

elseif(ci  STREQUAL  "asan")
    message("!!! asan+lsan+ubsan for: use-after-free, double-free, out-of-bound, leak, undef behavior")
    add_compile_definitions(_FORTIFY_SOURCE=2)     # buffer overflow checks
    add_compile_options(-g -fno-omit-frame-pointer)
    add_compile_options(-fstack-protector-strong)  # stack overflow protection
    add_compile_options(-O2)  # required for FORTIFY_SOURCE
    add_compile_options(-fsanitize=address,undefined)

    add_link_options(-fsanitize=address,undefined)
    add_link_options(-fstack-protector-strong)     # ensure stack protector runtime is linked

elseif(ci  STREQUAL  "tsan")  # asan/lsan/msan cover by valgrind(memcheck)
    message("!!! SANITIZE for: thread")
    add_compile_definitions(_FORTIFY_SOURCE=2)     # buffer overflow checks
    add_compile_options(-g -fno-omit-frame-pointer)
    add_compile_options(-fstack-protector-strong)  # stack overflow protection
    add_compile_options(-O2)  # required for FORTIFY_SOURCE
    add_compile_options(-fsanitize=thread)

    add_link_options(-fsanitize=thread)
    add_link_options(-fstack-protector-strong)     # ensure stack protector runtime is linked

elseif(ci  STREQUAL  "release")
    # release-perf build: cold/hot path separation comes from compiler flags, not source macros
    # - O3                              : enables block reordering, inlining, vectorization
    # - freorder-blocks-and-partition   : split hot vs cold blocks into .text vs .text.unlikely
    # - freorder-functions              : place mutually-called funcs adjacent (better i-cache)
    # - ffunction-sections/fdata-s.     : 1 section per func, lets linker drop dead code & re-pack hot
    # - Wl,--gc-sections                : actually drop unused sections at link time
    # - DNDEBUG                         : disable assert in hot path
    # NOTE: -flto is intentionally OFF here; with our toolchain it merges all blocks back into a
    #       single .text and defeats the section partitioning we want to verify.
    message("!!! release: max perf, hot/cold sections separated by compiler")
    add_compile_definitions(DOMLIB_UT)  # keep GOLD_perf_mem benchmark active
    add_compile_options(-O3 -DNDEBUG)
    add_compile_options(-freorder-blocks-and-partition -freorder-functions)
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)

else()
    message("!!! UT only - no cov, no SANITIZE check etc")
    add_compile_options(-O1)  # fastest build+ut (1-cpp-change=5s, vs 45s of -g)
    add_compile_definitions(DOMLIB_UT)  # benchmark etc
endif()

#add_compile_options(-fno-exceptions)  # inc branch coverage

add_executable(ut_exe)

# googletest via FetchContent (always latest, shallow clone to min download)
include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY  https://github.com/google/googletest.git
    GIT_TAG         main
    GIT_SHALLOW     TRUE
)
FetchContent_MakeAvailable(googletest)

add_subdirectory(src)
add_subdirectory(ut)

# C++ standard on targets (decided per ci mode above)
target_compile_features(lib_domino  PUBLIC  ${_cxx_std})

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

# simplify "make help"
set(CMAKE_SKIP_PREPROCESSED_SOURCE_RULES  true)
set(CMAKE_SKIP_ASSEMBLY_SOURCE_RULES      true)
