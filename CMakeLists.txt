cmake_minimum_required (VERSION 2.6)
project (mbm)

set(MLAB_LIBRARIES_ROOT ${PROJECT_SOURCE_DIR}/third_party/m-lab.libraries/trunk)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

if(DEFINED USE_WEB100 AND USE_WEB100)
	find_package(Web100 REQUIRED)
endif()

add_subdirectory(${MLAB_LIBRARIES_ROOT})
add_subdirectory(src/common)
add_subdirectory(src/client)
add_subdirectory(src/server)
