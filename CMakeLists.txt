# target a cmake version, you can target a lower version if you like
cmake_minimum_required(VERSION 3.24)

# declare our packer
project(packer CXX)

# this line will mark our packer as MultiThreaded, instead of MultiThreadedDLL
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# this will collect header, source and resource files into convenient variables
file(GLOB_RECURSE PACKER_SRC ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE PACKER_HDR ${PROJECT_SOURCE_DIR}/src/*.hpp)
file(GLOB_RECURSE PACKER_RC ${PROJECT_SOURCE_DIR}/src/*.rc)

# this will give you source groups in the resulting Visual Studio project
source_group(TREE "${PROJECT_SOURCE_DIR}" PREFIX "Header Files" FILES ${PACKER_HDR})
source_group(TREE "${PROJECT_SOURCE_DIR}" PREFIX "Source Files" FILES ${PACKER_SRC})
source_group(TREE "${PROJECT_SOURCE_DIR}" PREFIX "Resource Files" FILES ${PACKER_RC})

# this will add zlib as a build target
add_subdirectory(${PROJECT_SOURCE_DIR}/zlib-1.2.13)

# this will add our stub project
add_subdirectory(${PROJECT_SOURCE_DIR}/stub)

# this will add our test dummy project
add_subdirectory(${PROJECT_SOURCE_DIR}/dummy)

# this will create our packer executable
add_executable(packer ${PACKER_HDR} ${PACKER_SRC})
target_sources(packer PRIVATE ${PACKER_RC})

# this will link zlib to our packer
target_link_libraries(packer zlibstatic)

# zlib, as part of its build step, drops a config header in the build directory.
# make sure to include it as well as the original source in your includes!
target_include_directories(packer PUBLIC
  "${PROJECT_SOURCE_DIR}/src"
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
  COMMAND "${CMAKE_CURRENT_BINARY_DIR}/$<$<CONFIG:Debug>:Debug>/packer.exe"
  "${CMAKE_CURRENT_BINARY_DIR}/dummy/$<$<CONFIG:Debug>:Debug>/dummy.exe")
add_test(NAME test_unpack
  COMMAND "packed.exe")
