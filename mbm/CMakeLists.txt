cmake_minimum_required (VERSION 2.8)
project (mbm)

set(MLAB_LIBRARIES_ROOT ${PROJECT_SOURCE_DIR}/third_party/m-lab.libraries/trunk)

add_subdirectory(${MLAB_LIBRARIES_ROOT})
add_subdirectory(src/server)
