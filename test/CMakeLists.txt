# Copyright 2021 Lawrence Livermore National Security, LLC and other Privateer Project Developers.
# See the top-level LICENSE file for details.
#
# SPDX-License-Identifier: MIT

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

function(add_gtest_executable test_name test_file)
    add_executable(${test_name} ${test_file})
    target_link_libraries(${test_name} PRIVATE GTest::gtest GTest::gtest_main)
    gtest_discover_tests(${test_name})
endfunction()

add_gtest_executable(PrivateerTest PrivateerTest.cpp)
if (MPI_CXX_FOUND)
  target_link_libraries(PrivateerTest PUBLIC MPI::MPI_CXX)
else()
  message(STATUS "Will skip the MPI tests")
endif()

if (ENABLE_ZSTD)
  target_link_libraries(PrivateerTest PUBLIC zstd::zstd)
  target_compile_definitions(PrivateerTest PRIVATE USE_COMPRESSION)
endif()

#if (ENABLE_SPDLOG)
  target_link_libraries(PrivateerTest PUBLIC spdlog::spdlog)
#endif()

if (ENABLE_UFFD)
  target_compile_definitions(PrivateerTest PRIVATE USERFAULTFD)
else()
  target_compile_definitions(PrivateerTest PRIVATE SIGACTION)
endif()
