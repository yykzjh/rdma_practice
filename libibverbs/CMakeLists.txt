cmake_minimum_required(VERSION 3.20)

#
# libibverbs module: build small test programs against system libibverbs.
#

find_package(PkgConfig)

set(IBVERBS_FOUND 0)

if(PKG_CONFIG_FOUND)
  pkg_check_modules(IBVERBS libibverbs)
  if(IBVERBS_FOUND)
    # Apply pkg-config flags for all targets under this module.
    include_directories(${IBVERBS_INCLUDE_DIRS})
    link_directories(${IBVERBS_LIBRARY_DIRS})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${IBVERBS_CFLAGS_OTHER}")
  endif()
endif()

if(NOT IBVERBS_FOUND)
  # Fallback when pkg-config (or libibverbs.pc) is unavailable.
  find_path(IBVERBS_INCLUDE_DIR infiniband/verbs.h)
  find_library(IBVERBS_LIBRARY ibverbs)

  if(IBVERBS_INCLUDE_DIR AND IBVERBS_LIBRARY)
    set(IBVERBS_FOUND 1)
    set(IBVERBS_INCLUDE_DIRS ${IBVERBS_INCLUDE_DIR})
    set(IBVERBS_LIBRARIES ${IBVERBS_LIBRARY})
    include_directories(${IBVERBS_INCLUDE_DIRS})
  else()
    message(FATAL_ERROR
      "libibverbs not found. Please install libibverbs development package.\n"
      "Examples: Debian/Ubuntu: libibverbs-dev; RHEL/CentOS: libibverbs-devel")
  endif()
endif()

# Library: pingpong helpers
add_library(ibv_pingpong pingpong.cpp include/pingpong.hpp)
target_include_directories(ibv_pingpong PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ibv_pingpong PUBLIC ${IBVERBS_LIBRARIES})

# Library: utils
add_library(ibv_utils utils.cpp include/utils.hpp)
target_include_directories(ibv_utils PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Programs (one executable per .cpp file)
add_executable(ibv_device_list device_list.cpp)
target_link_libraries(ibv_device_list ${IBVERBS_LIBRARIES})
add_executable(ibv_devinfo devinfo.cpp)
target_link_libraries(ibv_devinfo ${IBVERBS_LIBRARIES})
add_executable(ibv_rc_pingpong rc_pingpong.cpp)
target_link_libraries(ibv_rc_pingpong ${IBVERBS_LIBRARIES} ibv_utils ibv_pingpong)