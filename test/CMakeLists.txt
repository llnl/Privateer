# Copyright 2021 Lawrence Livermore National Security, LLC and other Privateer Project Developers.
# See the top-level LICENSE file for details.
#
# SPDX-License-Identifier: MIT

find_package(GTest REQUIRED)

include(GoogleTest)

function(add_gtest_executable test_name test_file)
    add_executable(${test_name} ${test_file})
    target_link_libraries(${test_name} PRIVATE GTest::gtest GTest::gtest_main)
    # add_dependencies(${test_name} privateer-static)
    # target_link_libraries(${test_name} PUBLIC privateer-static)
    add_dependencies(${test_name} privateer)
    target_link_libraries(${test_name} PUBLIC privateer)
  set_target_properties(${test_name} PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
  )
    gtest_discover_tests(${test_name})
endfunction()

add_gtest_executable(PrivateerTest PrivateerTest.cpp)



if (MPI_CXX_FOUND)
  target_link_libraries(PrivateerTest PRIVATE MPI::MPI_CXX)
  get_target_property(privateer_test_mpi_link_libraries MPI::MPI_CXX INTERFACE_LINK_LIBRARIES)
  if(privateer_test_mpi_link_libraries)
    set_property(TARGET PrivateerTest APPEND PROPERTY BUILD_RPATH "/usr/tce/packages/mvapich2/mvapich2-2.3.7-gcc-10.3.1/lib")
  endif()
else()
  message(STATUS "Will skip the MPI tests")
endif()

set_property(TARGET PrivateerTest APPEND PROPERTY BUILD_RPATH "/collab/usr/global/tools/tce4/packages/gcc/gcc-10.3.1/lib/gcc/x86_64-redhat-linux/10")
