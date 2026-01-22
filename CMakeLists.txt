cmake_minimum_required(VERSION 3.20)

project(rdma_practice CXX)

# Keep all executables in one place.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Common flags (compatible with old CMake).
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

option(RDMA_PRACTICE_WERROR "Treat warnings as errors" OFF)
if(RDMA_PRACTICE_WERROR)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
endif()

add_subdirectory(libibverbs)

