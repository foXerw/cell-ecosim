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
  src/core/world.cpp
  src/util/error.cpp
)
target_include_directories(ecosim
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(ecosim PRIVATE ECOSIM_IMPLEMENTING=1)

# MinGW: ship a self-contained DLL, no MSYS2 runtime needed next to it.
target_link_options(ecosim PRIVATE -static -static-libgcc -static-libstdc++)
set_target_properties(ecosim PROPERTIES
  PREFIX ""
  OUTPUT_NAME ecosim
  SUFFIX ".dll"
)

add_executable(ecosim_tests
  tests/main.cpp
  tests/test_world.cpp
  tests/test_kernel.cpp
)
target_include_directories(ecosim_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(ecosim_tests PRIVATE ecosim)

enable_testing()
add_test(NAME native_core COMMAND ecosim_tests)
