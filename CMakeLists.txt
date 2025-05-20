# Copyright 2021 Lawrence Livermore National Security, LLC and other Privateer Project Developers.
# See the top-level LICENSE file for details.
#
# SPDX-License-Identifier: MIT

cmake_minimum_required (VERSION 3.5.1)
project( Privateer LANGUAGES CXX C )

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# create config file
include(CMakePackageConfigHelpers)
configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in"
        "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake)

set (CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

set (CMAKE_C_STANDARD 17)

# cmake_policy(SET CMP0074 NEW)

set( default_build_type "Release" )
if ( EXISTS "${CMAKE_SOURCE_DIR}/.git" )
  set( default_build_type "Debug" )
endif()




# OpenSSL
# link_libraries(OpenSSL::SSL)
# link_libraries(OpenSSL::Crypto)

# File System
# link_libraries(stdc++fs)


# ------------------OpenMP--------------------------
FIND_PACKAGE( OpenMP REQUIRED )

if(OPENMP_FOUND)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
  set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
else()
  message(FATAL_ERROR "Error: OpenMP not found")
endif()
# --------------------------------------------------

include(FetchContent)
# ------------------ Boost -------------------------------------------------------------------------------
set (Boost_NO_BOOST_CMAKE ON)

find_package(Boost) # COMPONENTS iostreams) # REQUIRED COMPONENTS system iostreams)

if (Boost_FOUND)

  # Just a sanity check
  if (NOT Boost_INCLUDE_DIRS)
    message(FATAL_ERROR "Something is wrong: Boost_INCLUDE_DIRS is empty although Boost_FOUND is true.")
  endif()
  include_directories(${Boost_INCLUDE_DIRS})
else ()
  message(STATUS "Download and setup Boost")
  FetchContent_Declare(Boost
    URL https://boostorg.jfrog.io/artifactory/main/release/1.82.0/source/boost_1_82_0.tar.bz2)
  FetchContent_GetProperties(Boost)
  if (NOT Boost_POPULATED)
    FetchContent_Populate(Boost)
  endif ()
  set(BOOST_ROOT ${boost_SOURCE_DIR})
  find_package(Boost 1.82)
  include_directories(${Boost_INCLUDE_DIRS})
endif ()

# ---------------------------------------------------------------------------------------------------------- 


include_directories(include)
include_directories(src)

set(BUILD_SHARED_LIBS ON)

set(privateer_headers include/privateer/c_api/privateer.h include/privateer/privateer.hpp)
set(privateer_src src/privateer_c.cpp ${privateer_headers})

add_library(privateer SHARED ${privateer_src} )
# add_library(privateer STATIC ${privateer_src} )

find_package(OpenSSL REQUIRED)
# TARGET_LINK_LIBRARIES(privateer rt)
target_link_libraries(privateer PUBLIC rt)
# TARGET_LINK_LIBRARIES(privateer OpenSSL::SSL)
target_link_libraries(privateer PUBLIC OpenSSL::SSL)
# TARGET_LINK_LIBRARIES(privateer OpenSSL::Crypto)
target_link_libraries(privateer PUBLIC OpenSSL::Crypto)

# File System
# TARGET_LINK_LIBRARIES(privateer stdc++fs)
target_link_libraries(privateer PUBLIC stdc++fs)

# ZSTD
# ------------------ZSTD----------------------------
option(ENABLE_ZSTD "Enable compression" OFF)
if (ENABLE_ZSTD)
  message(STATUS "Configuring with zstd")
  list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
  find_package(zstd REQUIRED)
  target_include_directories(privateer PUBLIC ${ZSTD_INCLUDE_DIRS})
  target_link_libraries(privateer PUBLIC zstd::zstd)
  target_compile_definitions(privateer PUBLIC USE_COMPRESSION)
endif()
# --------------------------------------------------

# SPDLOG
# ------------------SPDLOG----------------------------
#option(ENABLE_SPDLOG "Enable logging" OFF)
#if (ENABLE_SPDLOG)
message(STATUS "Configuring with spdlog")
FetchContent_Declare(spdlog
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz)
# FetchContent_Populate(spdlog)
FetchContent_MakeAvailable(spdlog)
install(TARGETS spdlog EXPORT PrivateerTargets)
FetchContent_GetProperties(spdlog BINARY_DIR spdlog_BINARY_DIR)
FetchContent_GetProperties(spdlog SOURCE_DIR spdlog_SOURCE_DIR)
include_directories(${spdlog_BINARY_DIR})
target_link_libraries(privateer PUBLIC spdlog::spdlog)
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

# get_target_property(SPDLOG_INCLUDE_DIR spdlog INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "spdlog include directory: ${spdlog_SOURCE_DIR}/include")

# get_target_property(SPDLOG_LIBRARIES spdlog INTERFACE_LINK_LIBRARIES)
message(STATUS "spdlog libraries: ${spdlog_BINARY_DIR}")

set_target_properties(spdlog PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${spdlog_SOURCE_DIR}/include"
    INTERFACE_LINK_LIBRARIES "${spdlog_BINARY_DIR}/libspdlog.so"
)

#endif()
# --------------------------------------------------

# iostreams
# TARGET_LINK_LIBRARIES(privateer boost_iostreams)
# TARGET_LINK_LIBRARIES(privateer boost_iostreams)

set_target_properties(privateer PROPERTIES OUTPUT_NAME privateer)

# install(TARGETS  privateer # Removed "privateer" from here
#         LIBRARY DESTINATION lib
#         ARCHIVE DESTINATION lib
#         RUNTIME DESTINATION bin )

# install(FILES include/privateer/privateer.hpp DESTINATION include/privateer)
# install(FILES include/privateer/c_api/privateer.h DESTINATION include/privateer/c_api)

install(DIRECTORY include/privateer DESTINATION include)

option(ENABLE_UFFD "Enable userfaultfd" OFF)
if (ENABLE_UFFD)
  target_compile_definitions(privateer PUBLIC USERFAULTFD)
  message(STATUS "Configuring with UFFD")
else()
  target_compile_definitions(privateer PUBLIC SIGACTION)
  message(STATUS "Configuring with SIGACTION")
endif()

option(ENABLE_PAGE_EVICTION "Enable page eviction" OFF)
if (ENABLE_PAGE_EVICTION)
  target_compile_definitions(privateer PUBLIC ENABLE_PAGE_EVICTION)
  message(STATUS "Configuring with Page Eviction")
else()
  message(STATUS "Configuring without Page Eviction")
endif()

option(PRIVATEER_USE_SMARTCACHE "Use SmartCache for block storage" OFF)
option(SMARTCACHE_ROOT "Path to existing SmartCache installation" "")

if(PRIVATEER_USE_SMARTCACHE)
  # Fetch SmartCache
  set(FETCHCONTENT_QUIET OFF)
  FetchContent_Declare(
    SmartCache
    GIT_REPOSITORY ssh://git@czgitlab.llnl.gov:7999/youssef2/smartcache.git
    GIT_TAG dev/v0.1_core # Replace with the correct branch or tag
  )
  # FetchContent_MakeAvailable(SmartCache)

  FetchContent_GetProperties(SmartCache)
  if (NOT SmartCache_POPULATED)
      FetchContent_Populate(SmartCache)
  endif()

  # Ensure SmartCache_SOURCE_DIR is set
  set(SmartCache_SOURCE_DIR ${smartcache_SOURCE_DIR})
  set(SmartCache_BINARY_DIR ${smartcache_BINARY_DIR})
  message(STATUS "SmartCache source directory explicitly set to: ${SmartCache_SOURCE_DIR}")
  message(STATUS "SmartCache binary directory explicitly set to: ${SmartCache_BINARY_DIR}")
  if (NOT SmartCache_SOURCE_DIR)
    message(FATAL_ERROR "SmartCache_SOURCE_DIR is not set. FetchContent failed.")
  endif()

  # Configure SmartCache to build dependencies
  add_custom_target(SmartCacheBuildDependencies ALL
  COMMAND ${CMAKE_COMMAND} -DSMARTCACHE_BUILD_DEPENDENCIES=ON 
          -DSMARTCACHE_PROFILER=DFTRACER
          -DSMARTCACHE_ENABLE_TESTS=OFF
          -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
          -B ${SmartCache_BINARY_DIR}
          -S ${SmartCache_SOURCE_DIR}
  COMMAND ${CMAKE_COMMAND} --build ${SmartCache_BINARY_DIR} --target all
  WORKING_DIRECTORY ${SmartCache_BINARY_DIR}
  )

  # Configure SmartCache to build its main code
  add_custom_target(SmartCacheBuildCode ALL
  COMMAND ${CMAKE_COMMAND} -DSMARTCACHE_BUILD_DEPENDENCIES=OFF 
          -DSMARTCACHE_ENABLE_TESTS=OFF
          -DSMARTCACHE_PROFILER=DFTRACER
          -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
          -B ${SmartCache_BINARY_DIR}
          -S ${SmartCache_SOURCE_DIR}
  COMMAND ${CMAKE_COMMAND} --build ${SmartCache_BINARY_DIR} --target all
  WORKING_DIRECTORY ${SmartCache_BINARY_DIR}
  )

  add_dependencies(SmartCacheBuildCode SmartCacheBuildDependencies)
  add_dependencies(privateer SmartCacheBuildCode)

  include_directories(${SmartCache_BINARY_DIR}/include)
  # link_directories(${SmartCache_BINARY_DIR}/usr/local/lib64)
  target_link_directories(privateer PUBLIC ${SmartCache_BINARY_DIR}/usr/local/lib64)
  target_compile_definitions(privateer PUBLIC USE_SMARTCACHE)
  target_link_libraries(privateer PUBLIC smartcache_client)
  target_link_libraries(privateer PUBLIC smartcache_core)
endif()

find_package(MPI)

# Export the library dependencies
install(DIRECTORY include/privateer DESTINATION include)
install(TARGETS privateer EXPORT PrivateerTargets DESTINATION lib)
install(EXPORT PrivateerTargets DESTINATION lib/cmake/privateer)

option (PRIVATEER_BUILD_TEST "Build Privateer test" Off)

if (PRIVATEER_BUILD_TEST)
  include(CTest)
  enable_testing()
  add_subdirectory(test)
endif()

add_subdirectory(examples)
