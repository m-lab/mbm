cmake_minimum_required (VERSION 2.6)

if(DEFINED CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Choose the build type.")
else()
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type.")
endif()

project(mbm_server)

set(PROJECT_ROOT_DIR ${PROJECT_SOURCE_DIR}/../..)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_ROOT_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_ROOT_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_ROOT_DIR}/lib)

# Set up directories
set(MLAB_LIBRARIES_ROOT ${PROJECT_ROOT_DIR}/third_party/m-lab.libraries/trunk)
set(GTEST_ROOT ${MLAB_LIBRARIES_ROOT}/third_party/gtest-1.6.0)
set(JSONCPP_ROOT ${MLAB_LIBRARIES_ROOT}/third_party/json-cpp)

set(CMAKE_CXX_FLAGS "-Wall -Werror -fPIC -fno-exceptions -fno-rtti")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -fno-strict-aliasing")

add_definitions(-DOS_LINUX)
if(DEFINED USE_PCAP AND USE_PCAP)
	add_definitions(-DUSE_PCAP)
	set(MBM_PCAP_INCLUDE_DIRS ${PCAP_INCLUDE_DIRS})
	set(MBM_PCAP_LIBRARIES ${PCAP_LIBRARIES})
else()
	set(MBM_PCAP_INCLUDE_DIRS "")
	set(MBM_PCAP_LIBRARIES "")
endif()

# Set CPU
if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86")
	add_definitions(-DARCH_X86)
endif()

include_directories(
	${PROJECT_ROOT_DIR}/src
	${MLAB_LIBRARIES_ROOT}/include
	${GTEST_ROOT}/include
	${MBM_PCAP_INCLUDE_DIRS})
link_directories(
        ${PROJECT_ROOT_DIR}/lib
	${MLAB_LIBRARIES_ROOT}/lib
        ${JSONCPP_ROOT}/lib)

file(GLOB_RECURSE SRC_FILES *.cc)

# Build the libraries
add_executable(mbm_server ${SRC_FILES})
target_link_libraries(mbm_server mlab mbm ${MBM_PCAP_LIBRARIES} rt json-cpp)
