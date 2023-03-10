# target a cmake version, you can target a lower version if you like
cmake_minimum_required(VERSION 3.24)

# declare our packer as a C++ project (since zlib is a C project and the compilation
# detection might get confused)
project(packer CXX)

# this line will mark our packer as MultiThreaded, instead of MultiThreadedDLL
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# this will collect header, source and resource files into convenient variables
file(GLOB_RECURSE SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE HDR_FILES ${PROJECT_SOURCE_DIR}/src/*.hpp)
file(GLOB_RECURSE RC_FILES ${PROJECT_SOURCE_DIR}/src/*.rc)

# this will give you source groups in the resulting Visual Studio project
source_group(TREE "${PROJECT_SOURCE_DIR}" PREFIX "Header Files" FILES ${HDR_FILES})
source_group(TREE "${PROJECT_SOURCE_DIR}" PREFIX "Source Files" FILES ${SRC_FILES})
source_group(TREE "${PROJECT_SOURCE_DIR}" PREFIX "Resource Files" FILES ${RC_FILES})

# this will add zlib as a build target
add_subdirectory(${PROJECT_SOURCE_DIR}/zlib-1.2.13)

# this will add our stub project
add_subdirectory(${PROJECT_SOURCE_DIR}/stub)

# this will add our test dummy project
add_subdirectory(${PROJECT_SOURCE_DIR}/dummy)

# this will make sure our stub data will be included in the resources of our packer
# despite where it may reside in cmake's build system
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/stub.hpp"
  CONTENT "#pragma once\n#define IDB_STUB 1000\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/stub.rc"
  CONTENT "#include <winresrc.h>\n#include \"stub.hpp\"\nIDB_STUB STUB \"$<TARGET_FILE:stub>\"\n")

# this will create our packer executable
add_executable(packer ${HDR_FILES} ${SRC_FILES})
target_sources(packer PRIVATE ${RC_FILES} "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/stub.rc")

# this will link zlib to our packer
target_link_libraries(packer zlibstatic)

# zlib, as part of its build step, drops a config header in the build directory.
# we do this too, so make sure to include everything for the build!
target_include_directories(packer PUBLIC
  "${PROJECT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
  "${PROJECT_SOURCE_DIR}/zlib-1.2.13"
  "${CMAKE_CURRENT_BINARY_DIR}/zlib-1.2.13"
)

# also set the includes for the stub from here.
# we can't set this in the stub CMake file because CMake requires includes to be in the same
# directory as the build target. for this file, our build target is packer, so this sets
# up includes relative to the packer executable.
target_include_directories(stub PUBLIC
  "${PROJECT_SOURCE_DIR}/zlib-1.2.13"
  "${CMAKE_CURRENT_BINARY_DIR}/zlib-1.2.13"
)

# this will add our stub as a dependency and our dummy as being dependent on the packer.
add_dependencies(packer stub)
add_dependencies(dummy packer)

# enable testing to verify our packer works
enable_testing()
add_test(NAME test_pack
  COMMAND "$<TARGET_FILE:packer>"
  "$<TARGET_FILE:dummy>")
add_test(NAME test_unpack
  COMMAND "packed.exe")
