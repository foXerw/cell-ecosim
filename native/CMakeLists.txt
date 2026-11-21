cmake_minimum_required(VERSION 3.25)
project(cell_ecosim CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Everything lands in one directory so the test binaries can find ecosim.dll and
# so the managed side has a single predictable path (docs/building.md).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# MinGW: ship self-contained binaries, no MSYS2 runtime needed next to them.
set(ECOSIM_STATIC_LINK -static -static-libgcc -static-libstdc++)

# The engine as a static core. Both consumers below take it from here:
# the DLL (for the managed app) and the native binaries (tests, CLI), which
# link it directly. On this toolchain, linking an executable against a freshly
# built *import library* fails without any diagnostic from ld, so the native
# binaries deliberately do not go through ecosim.dll's .a — they compile the
# same ABI shell over the same objects.
add_library(ecosim_core STATIC
  src/core/grid.cpp
  src/core/kernel.cpp
  src/core/kernel_fast.cpp
  src/core/parallel.cpp
  src/core/render.cpp
  src/core/rle.cpp
  src/core/species.cpp
  src/core/world.cpp
  src/util/error.cpp
)
target_include_directories(ecosim_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_options(ecosim_core PRIVATE ${ECOSIM_STATIC_LINK})

# The shared library the C# side binds to.
add_library(ecosim SHARED src/abi/ecosim_abi.cpp)
target_include_directories(ecosim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(ecosim PRIVATE ECOSIM_IMPLEMENTING=1)
target_link_libraries(ecosim PRIVATE ecosim_core)
target_link_options(ecosim PRIVATE ${ECOSIM_STATIC_LINK})
set_target_properties(ecosim PROPERTIES
  PREFIX ""
  OUTPUT_NAME ecosim
  SUFFIX ".dll"
)

# Native tests: same ABI surface, statically linked.
add_executable(ecosim_tests
  src/abi/ecosim_abi.cpp
  tests/main.cpp
  tests/test_world.cpp
  tests/test_kernel.cpp
  tests/test_fast_kernel.cpp
  tests/test_parallel.cpp
  tests/test_rle.cpp
  tests/test_patterns.cpp
  tests/test_render.cpp
  tests/test_paint.cpp
  tests/test_species.cpp
)
target_compile_definitions(ecosim_tests PRIVATE
  ECOSIM_LINK_STATIC=1
  ECOSIM_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data/patterns")
target_include_directories(ecosim_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(ecosim_tests PRIVATE ecosim_core)
target_link_options(ecosim_tests PRIVATE ${ECOSIM_STATIC_LINK})

# Headless consumer of the same ABI the app binds to.
add_executable(ecosim_cli
  src/abi/ecosim_abi.cpp
  tools/ecosim-cli/main.cpp
)
set_target_properties(ecosim_cli PROPERTIES OUTPUT_NAME ecosim-cli)
target_compile_definitions(ecosim_cli PRIVATE ECOSIM_LINK_STATIC=1)
target_include_directories(ecosim_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ecosim_cli PRIVATE ecosim_core)
target_link_options(ecosim_cli PRIVATE ${ECOSIM_STATIC_LINK})

enable_testing()
add_test(NAME native_core COMMAND ecosim_tests)
add_test(NAME cli_smoke COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke.sh
                                $<TARGET_FILE:ecosim_cli>)
