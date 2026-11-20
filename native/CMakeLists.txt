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

add_library(ecosim SHARED
  src/abi/ecosim_abi.cpp
  src/core/grid.cpp
  src/core/kernel.cpp
  src/core/kernel_fast.cpp
  src/core/parallel.cpp
  src/core/world.cpp
  src/util/error.cpp
)
target_include_directories(ecosim
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(ecosim PRIVATE ECOSIM_IMPLEMENTING=1)

# MinGW: ship self-contained binaries, no MSYS2 runtime needed next to them.
# Without this, ecosim_tests.exe only starts if ucrt64/bin happens to be on
# PATH, which is exactly the kind of "works on my shell" trap to avoid.
set(ECOSIM_STATIC_LINK -static -static-libgcc -static-libstdc++)
target_link_options(ecosim PRIVATE ${ECOSIM_STATIC_LINK})
set_target_properties(ecosim PROPERTIES
  PREFIX ""
  OUTPUT_NAME ecosim
  SUFFIX ".dll"
)

add_executable(ecosim_tests
  tests/main.cpp
  tests/test_world.cpp
  tests/test_kernel.cpp
  tests/test_fast_kernel.cpp
  tests/test_parallel.cpp
  # Kernel and pool unit tests call the C++ classes directly, so they compile
  # those sources in instead of reaching through the DLL's C ABI.
  src/core/grid.cpp
  src/core/kernel.cpp
  src/core/kernel_fast.cpp
  src/core/parallel.cpp
)
target_include_directories(ecosim_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(ecosim_tests PRIVATE ecosim)
target_link_options(ecosim_tests PRIVATE ${ECOSIM_STATIC_LINK})

enable_testing()
add_test(NAME native_core COMMAND ecosim_tests)
