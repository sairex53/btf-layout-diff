cmake_minimum_required(VERSION 3.20)
project(btf_layout_diff VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(btf_layout btf_layout.cpp)
target_include_directories(btf_layout PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(btf_layout PRIVATE -Wall -Wextra -Wpedantic)

add_executable(btf-layout-diff main.cpp)
target_link_libraries(btf-layout-diff PRIVATE btf_layout)
target_compile_options(btf-layout-diff PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
  add_executable(btf-layout-tests test_btf_layout.cpp)
  target_link_libraries(btf-layout-tests PRIVATE btf_layout)
  target_compile_options(btf-layout-tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME btf-layout-tests COMMAND btf-layout-tests)
endif()
