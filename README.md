![PACKERS](https://frank2.github.io/img/packers.png "PACKERS")

## What is a packer?

A **packer** is a program that decompresses and launches another program within its address space. It is sometimes known for being the vector that attacks analysis environments, such as debuggers and virtual sandboxes. It's primarily used for a few things:

* **Compression**: Packers are commonly employed to compress the code of a given binary. This is one of its few legitimate uses. See [UPX](https://github.com/upx/upx) for an example of a compressing packer.
* **Obfuscation**: Packers are also employed when attempting to obfuscate or otherwise defend a program from reverse engineering. See [Riot Games's packman packer](https://www.unknowncheats.me/forum/league-of-legends/428115-inside-anti-cheat-packman.html) for an example of an anti-reversing packer.
* **Evasion**: Malware frequently uses a variety of packers in order to evade antivirus and even EDR. See [this analysis of a SmokeLoader packer](https://malwarology.substack.com/p/malicious-packer-pkr_ce1a?r=1lslzd) for an example of an evasive packer.

Fundamentally, a packer only has a few basic steps:

1. **Compression stage**: This is where the original executable is either compressed, obfuscated or both, into a new binary.
2. **Decompression stage**: This is where the packed executable decompresses or deobfuscates its original executable for loading.
3. **Loading stage**: This is where the packer mimics a variety of steps similar to the host platform's executable loader. Because of the complexity of executable binaries, this is the most complicated step, depending on the level of depth you wish to emulate.
4. **Execution stage**: This is where code transfers from the host stub executable into the newly loaded (i.e., unpacked) code.

Equally as simple, a packer only consists of a few pieces:

* **The packer**: This piece is responsible for preparing what's called the *stub executable* and giving it the data required to unpack a given binary. This is ultimately the **compression stage**.
* **The stub**: This binary is the piece of code responsible for decompressing, loading, and executing the original binary. As you can see, the stub executable does most of the heavy lifting for a packer.

Building a packer can be tricky, since the stub needs to be built and imported somehow into the packer executable. For Windows, learning the build system for Visual Studio beyond simple compilation can be a laborious chore. Luckily, [CMake](https://cmake.org) provides a simple, cross-platform build system that supports Visual Studio, and is highly customizable!

This tutorial aims to teach the following:

* How to segment and integrate the two main pieces of a packer into a cohesive, testable build environment.
* How to manipulate a Windows executable to add additional, loadable data.
* How to navigate a Windows executable to retrieve and load arbitrary data.
* How to emulate parts of the Windows loader to load and execute a Windows executable.

## Prerequisites

* **Knowledge of C++**: This tutorial is kind of useless if you don't know C++, as we're going to be doing pointer arithmetic a lot.
* **Visual Studio**: [Visual Studio](https://visualstudio.microsoft.com/) contains a fully featured Windows C++ compiler. This project is tested against Visual Studio 2019, but newer versions should be fine.
* **CMake**: [CMake](https://cmake.org) is the build system we use to help instrument our builds for the Visual Studio compiler.

## Getting a taste

First, let's prove, somehow, that this build system works and creates a properly packed executable. Once you've got CMake and Visual Studio installed, navigate to the root directory of this repository with your terminal of choice and execute the following:

```
$ mkdir build
$ cd build
$ cmake ../
```

This will create the necessary project files to build the packer tutorial code. Then run:

```
$ cmake --build ./ --config Release
```

This will build the packer project in `Release` mode. Then, you can run the following test:

```
$ ctest -C Release ./
```

If all goes well, test\_pack and test_unpack should succeed. If you want to see the results of the packing yourself, you should see this:

```
$ ./packed.exe
I'm just a little guy!
```

Let's talk about all the moving parts that made this happen.

## Sketching out our CMake project

So we know what the main components of a packer are, but how can we test our packer immediately as part of the development cycle? We'll need to add a third binary to our overall project to properly test the pack and unpack process of our binary. So overall, we need three projects:

* the packer
* the stub
* the dummy executable to pack

We also need a compression library to make sure the binary compresses into the stub executable. [zlib](https://zlib.net) will work out nicely for this.

Our project hierarchy, to start out with, should look like this:

```
packer/
+---+ CMakeLists.txt
    + dummy/
    | |
    | +---+ CMakeLists.txt
    |     + src/
    |       |
    |       +---+ main.cpp
    |
    + stub/
    | |
    | +---+ CMakeLists.txt
    |     + src/
    |       |
    |       +---+ main.cpp
    |
    + src/
    | |
    | +---+ main.cpp
    |
    + zlib-1.2.13/
      |
      +---+ CMakeLists.txt
          + ...
```

Our main.cpp in each folder can simply look like this for now:

```cpp
#include <iostream>

int main(int argc, char *argv[]) {
    std::cout << "I'm just a little guy!" << std::endl;
    
    return 0;
}
```

Already, we should establish that we have a simple dependency chain to deal with, which CMake will nicely resolve for us with some instrumentation:

* *packer* and *stub* depend on **zlib**
* *packer* depends on **stub**
* *dummy* depends on **packer** (because it needs to be packed by the packer)

Let's start with the [root project](https://github.com/frank2/packer-tutorial/blob/main/CMakeLists.txt), the packer, for our CMake instrumentation.

CMake usually requires a minimum version to deal with, since it's been around a long time and supports long-term use of prior versions. After that, we can declare our packer project as a C++ project.

```cmake
# target a cmake version, you can target a lower version if you like
cmake_minimum_required(VERSION 3.24)

# declare our packer as a C++ project (since zlib is a C project and the compilation
# detection might get confused)
project(packer CXX)
```

We also want to declare our packer as "MultiThreaded" instead of "MultiThreadedDLL" so we don't have to worry about runtime DLL dependencies.

```cmake
# this line will mark our packer as MultiThreaded, instead of MultiThreadedDLL
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
```

The statement in angle brackets in the variable setting statement is called a [generator expression](https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html) and helps us resolve configure-time data where needed, you'll see it a lot throughout this file. What this generator expression does is emit the string `Debug` when it detects that the config is `Debug`, and emits nothing otherwise. This provides a runtime library of `MultiThreadedDebug` when a compilation profile of `Debug` is selected, and `MultiThreaded` when a non-debug compilation profile is selected, such as `Release`. See [conditional generator expressions](https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html#conditional-expressions) to get a good understanding of what's going on there. For more about the `CMAKE_MSVC_RUNTIME_LIBRARY` variable, see [the CMake documentation](https://cmake.org/cmake/help/latest/variable/CMAKE_MSVC_RUNTIME_LIBRARY.html).

CMake will allow you to organize your source code into hierarchies like Visual Studio automatically does when creating projects with their UI, and it will also search recursively in your folders for matching filenames. In our config file, we set a global recurse on headers (.hpp), code (.cpp), and resource scripts (.rc). For our example, we only really need main.cpp, but this is useful to know for larger projects.

```cmake
# this will collect header, source and resource files into convenient variables
file(GLOB_RECURSE SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE HDR_FILES ${PROJECT_SOURCE_DIR}/src/*.hpp)
file(GLOB_RECURSE RC_FILES ${PROJECT_SOURCE_DIR}/src/*.rc)

# this will give you source groups in the resulting Visual Studio project
source_group(TREE "${PROJECT_SOURCE_DIR}" PREFIX "Header Files" FILES ${HDR_FILES})
source_group(TREE "${PROJECT_SOURCE_DIR}" PREFIX "Source Files" FILES ${SRC_FILES})
source_group(TREE "${PROJECT_SOURCE_DIR}" PREFIX "Resource Files" FILES ${RC_FILES})
```

Here I should explain that CMake is more accurately described as a **make system**. It creates the proper "make system" for the given environment based on the given compiler. On Linux, this would be a makefile for the detected (or provided) compilers. For us on Windows, this produces a Visual Studio project compatible with our version of Visual Studio, meaning once you create the MSVC make system you can just use Visual Studio for everything if you so wished! Ultimately, you're using CMake to configure Visual Studio. So what you're doing here is essentially creating the trees of the files in your project in Visual Studio that the GUI does for you when creating a project.

Next, we need to add our dependent projects:

```cmake
# this will add zlib as a build target
add_subdirectory(${PROJECT_SOURCE_DIR}/zlib-1.2.13)

# this will add our stub project
add_subdirectory(${PROJECT_SOURCE_DIR}/stub)

# this will add our test dummy project
add_subdirectory(${PROJECT_SOURCE_DIR}/dummy)
```

One thing I mentioned earlier is that the packer project depends on the stub project. The packer, somehow, needs to retain the stub and manipulate it to eventually get our packed executable. We can use [Windows resource files](https://learn.microsoft.com/en-us/windows/win32/menurc/about-resource-files) to eventually stick our stub executable in our packer binary, no matter the build configuration! In addition to this, we can use CMake to generate those files for us so that our references to the built executables are sound within the CMake project! Let's generate our resource files for now so we can include them in our project:

```cmake
# this will make sure our stub data will be included in the resources of our packer
# despite where it may reside in cmake's build system
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/stub.hpp"
  CONTENT "#pragma once\n#define IDB_STUB 1000\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/stub.rc"
  CONTENT "#include <winresrc.h>\n#include \"stub.hpp\"\nIDB_STUB STUB \"$<TARGET_FILE:stub>\"\n")
```

`CMAKE_CURRENT_BINARY_DIR` is the string that contains your current build directory. Visual Studio dumps binaries into folders based on their configuration, so we use the `$<CONFIG>` generator statement to get the current configuration of the build. We also use the `$<TARGET_FILE:stub>` generator statement to output the executable file name of the stub binary once it's been compiled. When we include these files into our project-- the generated RC file and the generated header-- we can successfully integrate our stub binary into the packer project.

Next, we declare an executable for our packer project with the scooped up files from earlier:

```cmake
# this will create our packer executable
add_executable(packer ${HDR_FILES} ${SRC_FILES})
target_sources(packer PRIVATE ${RC_FILES} "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/stub.rc")
```

Then we link the imported zlib library for the linker:

```cmake
# this will link zlib to our packer
target_link_libraries(packer zlibstatic)
```

You can instead link just `zlib` if you really want the DLL of zlib.

Because we have generated files (and so does zlib as part of its build step), we need to include the dynamic directories in the include headers for the project. Because of a dependency on projects being in the root of a project in CMake, we also add the zlib include directories for our stub binary as well:

```cmake
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
```

Finally, we smooth things over in our dependency chain by letting CMake know about our dependency situation: mark the stub as a depdency of the packer and the packer as a dependency of the dummy.

```cmake
# this will add our stub as a dependency and our dummy as being dependent on the packer.
add_dependencies(packer stub)
add_dependencies(dummy packer)
```

The CMake files for [the stub](https://github.com/frank2/packer-tutorial/blob/main/stub/CMakeLists.txt) and [the dummy](https://github.com/frank2/packer-tutorial/blob/main/dummy/CMakeLists.txt) are easy to understand at this point in time, so I won't explain them. But what's great is that CMake can manage testing for us! And at this point, generating testing for our packer is a smooth process: we can simply issue a command, and if the exit code is 0, the test passes. For simplicity's sake, let's say we pack a binary by giving it to the first argument of the executable. We want something like this:

```
$ packer.exe dummy.exe
```

To do that with CMake is very simple:

```cmake
# enable testing to verify our packer works
enable_testing()
add_test(NAME test_pack
  COMMAND "$<TARGET_FILE:packer>"
  "$<TARGET_FILE:dummy>")
```

Finally, testing whether or not the program unpacks is even simpler: simply run the output! It shouldn't be surprising how many errors you'll run into that crash the program simply by running when you're attempting to emulate the loader! Let's say again, for simplicity's sake, that the binary should output to "packed.exe" for a packed binary. In that case, all you have to do is this:

```cmake
add_test(NAME test_unpack
  COMMAND "packed.exe")
```

This will gracefully fail if your packer failed to output the binary. Unfortunately, CMake for Visual Studio ignores the `ADDITIONAL_CLEAN_FILES` variable, so you'll have to manually clean all generated files in your build system, including that of the generated stub.rc and stub.hpp files above.

Congratulations! A lot of work has been done to successfully build and test your packer. Now that we've eaten our vegetables, our packer can do the following:

* compile all dependencies in the right order
* build the stub binary and inject it into our packer binary
* test the packing/unpacking process automatically

Now we can move on to the good stuff!

## Packing binaries into your stub
