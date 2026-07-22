# Copyright 2021 Lawrence Livermore National Security, LLC and other Privateer Project Developers.
# See the top-level LICENSE file for details.
#
# SPDX-License-Identifier: MIT

project(incremental_update_snapshot)

set (CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

set (CMAKE_C_STANDARD 17)

find_package ( OpenMP REQUIRED )

if(OPENMP_FOUND)
  add_executable(incremental_update_snapshot incremental_update_snapshot.cpp)
  # add_dependencies(incremental_update_snapshot privateer-static)
  # target_link_libraries(incremental_update_snapshot PUBLIC privateer-static)
  add_dependencies(incremental_update_snapshot privateer)
  target_link_libraries(incremental_update_snapshot PUBLIC privateer OpenMP::OpenMP_CXX)

  add_executable(incremental_update_snapshot_skewed incremental_update_snapshot_skewed.cpp)
  # add_dependencies(incremental_update_snapshot_skewed privateer-static)
  # target_link_libraries(incremental_update_snapshot_skewed PUBLIC privateer-static)
  add_dependencies(incremental_update_snapshot_skewed privateer)
  target_link_libraries(incremental_update_snapshot_skewed PUBLIC privateer OpenMP::OpenMP_CXX)

else()
  message("Skipping incremental_update_snapshot, OpenMP required")
endif()
