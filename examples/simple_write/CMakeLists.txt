project(simple_write)

set (CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_EXTENSIONS OFF)
set (CMAKE_C_STANDARD 17)
find_package ( OpenMP REQUIRED )
# Find the Privateer library
if(OPENMP_FOUND)
  add_executable(simple_write simple_write.cpp)
  # add_dependencies(simple_write privateer-static)
  # target_link_libraries(simple_write PUBLIC privateer-static)
  add_dependencies(simple_write privateer)
  target_link_libraries(simple_write PUBLIC privateer OpenMP::OpenMP_CXX)

else()
  message("Skipping simple_write, OpenMP required")
endif()