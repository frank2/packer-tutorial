![PACKERS](https://frank2.github.io/img/packers.png "PACKERS")

### Table of Contents
1. **[What is a packer?](#what-is-a-packer)**: An introduction into the purpose of packers and a guide through what the development necessities of a packer are.
2. **[Prerequisites](#prerequisites)**: Tools needed to work with this tutorial.
3. **[Getting a taste](#getting-a-taste)**: A demonstration of what you will be building with this tutorial.
4. **[Sketching out our CMake project](#sketching-out-our-cmake-project)**: A mini CMake tutorial on creating a build system for the somewhat complex needs of a packer.
5. **[Packing binaries into your stub](#packing-binaries-into-your-stub)**: A tutorial on how to write the packer portion of the packer/stub combo, and also an introduction to the Windows executable file format.
   1. [Managing resources](#managing-resources)
   2. [Parsing a PE file](#parsing-a-pe-file)
   3. [Manipulating a PE file](#manipulating-a-pe-file)
6. **[Simulating the loader](#simulating-the-loader)**: A tutorial on how to build a minimal stub executable to unpack and load a target executable, and also an introduction into more advanced Windows executable manipulation.
   1. [Reading our PE from memory](#reading-our-pe-from-memory)
   2. [Loading our PE for execution](#loading-our-pe-for-execution)
   3. [Resolving API imports](#resolving-api-imports)
   4. [Resolving addresses](#resolving-addresses)
   5. [Transferring execution](#transferring-execution)
7. **[Further exercises](#further-exercises)**: Some exercises to further expand on your packer development knowledge.

Seem like a daunting read? Try [the presentation version](https://github.com/frank2/packer-tutorial/blob/main/presentation.pdf), which summarizes this readme. YouTube video coming soon!

## What is a packer?

A **packer** is a program that decompresses and launches another program within its address space (or sometimes, another process's address space). It is sometimes known for being the vector that attacks analysis environments, such as debuggers and virtual sandboxes. It's primarily used for a few things:

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

If you're already familiar with C++ and CMake, feel free to skip [directly to the packing section](https://github.com/frank2/packer-tutorial#packing-binaries-into-your-stub). Otherwise, keep reading!

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

We also want to declare our packer as "MultiThreaded" (i.e., /MT) instead of "MultiThreadedDLL" (i.e., /MD) so we don't have to worry about runtime DLL dependencies.

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

The CMake files for [the stub](https://github.com/frank2/packer-tutorial/blob/main/stub/CMakeLists.txt) and [the dummy](https://github.com/frank2/packer-tutorial/blob/main/dummy/CMakeLists.txt) should be pretty easy to understand now!

What's great is that CMake can manage testing for us! And at this point, generating testing for our packer is a smooth process: we can simply issue a command, and if the exit code is 0, the test passes. For simplicity's sake, let's say we pack a binary by giving it to the first argument of the executable. We want something like this:

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

So we've successfully set up our compiler to compile the stub executable as a resource into our packer binary, but how do we get a binary into a packed state into the stub? We can't add it as a resource because we can't expect the end user of the packer to just use a compiler, the packer executable is supposed to be a stand-alone solution.

One technique I've used a lot (although it can be obvious to analysis) is adding a new section to the stub binary to eventually be loaded at runtime. This will wind up doubling as a crash-course in the PE format. But first, how do we get the data out of the resources?

### Managing resources

There are three basic steps to acquiring resource data out of a binary at runtime:

* Find the resource
* Load the resource
* Lock the resource (which gets the bytes of the resource)

The following function demonstrates how to search for and acquire a resource from a given binary:

```cpp
std::vector<std::uint8_t> load_resource(LPCSTR name, LPCSTR type) {
   auto resource = FindResourceA(nullptr, name, type);

   if (resource == nullptr) {
      std::cerr << "Error: couldn't find resource." << std::endl;
      ExitProcess(6);
   }

   auto rsrc_size = SizeofResource(GetModuleHandleA(nullptr), resource);
   auto handle = LoadResource(nullptr, resource);

   if (handle == nullptr) {
      std::cerr << "Error: couldn't load resource." << std::endl;
      ExitProcess(7);
   }

   auto byte_buffer = reinterpret_cast<std::uint8_t *>(LockResource(handle));

   return std::vector<std::uint8_t>(&byte_buffer[0], &byte_buffer[rsrc_size]);
}
```

With our data in an easily maleable vector, we can now parse the stub image and add new data to it.

### Parsing a PE file

A Windows executable, at its most basic components, is divided into two parts: its *headers* and its *section data*. The headers contain a lot of metadata important to the loading process, and the section data is just that-- data, which can be executable code (i.e., the `.text` section) or arbitrary data (i.e., a `.data` section). The start of every Windows executable begins with an `IMAGE_DOS_HEADER` structure:

```c
typedef struct _IMAGE_DOS_HEADER {      // DOS .EXE header
    WORD   e_magic;                     // Magic number
    WORD   e_cblp;                      // Bytes on last page of file
    WORD   e_cp;                        // Pages in file
    WORD   e_crlc;                      // Relocations
    WORD   e_cparhdr;                   // Size of header in paragraphs
    WORD   e_minalloc;                  // Minimum extra paragraphs needed
    WORD   e_maxalloc;                  // Maximum extra paragraphs needed
    WORD   e_ss;                        // Initial (relative) SS value
    WORD   e_sp;                        // Initial SP value
    WORD   e_csum;                      // Checksum
    WORD   e_ip;                        // Initial IP value
    WORD   e_cs;                        // Initial (relative) CS value
    WORD   e_lfarlc;                    // File address of relocation table
    WORD   e_ovno;                      // Overlay number
    WORD   e_res[4];                    // Reserved words
    WORD   e_oemid;                     // OEM identifier (for e_oeminfo)
    WORD   e_oeminfo;                   // OEM information; e_oemid specific
    WORD   e_res2[10];                  // Reserved words
    LONG   e_lfanew;                    // File address of new exe header
} IMAGE_DOS_HEADER, *PIMAGE_DOS_HEADER;
```

While this structure looks like it has a lot going on, you can probably tell by the name that this header is a relic of past versions of Windows and Microsoft DOS. Here, we only concern ourself with two values in this header: **e_magic** and **e_lfanew**. *e_magic* is simply the magic header value at the top of the image, the "MZ" at the beginning of the file. *e_lfanew* is the offset to the NT headers from the beginning of the file, which are the PE headers containing much more metadata information about the executable. We can, for example, construct a simple PE validator for our packer like so:

```cpp
void validate_target(const std::vector<std::uint8_t> &target) {
   auto dos_header = reinterpret_cast<const IMAGE_DOS_HEADER *>(target.data());

   // IMAGE_DOS_SIGNATURE is 0x5A4D (for "MZ")
   if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
   {
      std::cerr << "Error: target image has no valid DOS header." << std::endl;
      ExitProcess(3);
   }

   auto nt_header = reinterpret_cast<const IMAGE_NT_HEADERS *>(target.data() + dos_header->e_lfanew);

   // IMAGE_NT_SIGNATURE is 0x4550 (for "PE")
   if (nt_header->Signature != IMAGE_NT_SIGNATURE)
   {
      std::cerr << "Error: target image has no valid NT header." << std::endl;
      ExitProcess(4);
   }

   // IMAGE_NT_OPTIONAL_HDR64_MAGIC is 0x020B
   if (nt_header->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
   {
      std::cerr << "Error: only 64-bit executables are supported for this example!" << std::endl;
      ExitProcess(5);
   }
}
```

`IMAGE_NT_HEADERS` is a rather large structure overall, so I won't document the full thing here, but you can find everything you need to know about it on [Microsoft's documentation of the header](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_nt_headers64). We'll only be needing a few structure members from these headers anyhow. For now, we should compress our target binary for addition to our stub data.

Using zlib's `compress` and `decompress` functions are very straight-forward. If you really want to get fancy with working with zlib, I suggest working with the `deflate`/`inflate` functions, which allow you to work on compression streams chunks at a time. See the [zlib manual](https://www.zlib.net/manual.html)'s "Advanced Functions" section. For this example, though, `compress` and `decompress` will suffice.

To start, we acquire a size value with zlib's `compressBound` function on the size of our target binary. This size value corresponds to the maximum value needed to hold a compressed data stream given the data's size. We can then use this value to allocate a vector to contain the compressed data. The `compress` function eventually returns the true size of the compressed buffer, to which we can resize our vector to the proper size.

```cpp
// get the maximum size of a compressed buffer of the target binary's size.
uLong packed_max = compressBound(target.size());
uLong packed_real = packed_max;

// allocate a vector with that size
std::vector<std::uint8_t> packed(packed_max);
   
if (compress(packed.data(), &packed_real, target.data(), target.size()) != Z_OK)
{
   std::cerr << "Error: zlib failed to compress the buffer." << std::endl;
   ExitProcess(8);
}

// resize the buffer to the real compressed size
packed.resize(packed_real);
```

### Manipulating a PE file

Let's take a moment to talk about [data alignment](https://en.wikipedia.org/wiki/Data_structure_alignment). A given data stream is considered *aligned* if its address or size is divisible by a given *alignment boundary*. For example, in PE files, data sections on disk are typically aligned to the `0x400` boundary, whereas in memory they are aligned to the `0x1000` boundary. We can determine if a given value is aligned by performing a modulus of the alignment on the value (i.e., value % alignment == 0). PE files can be arbitrarily aligned to other values, and this value is present within and important to the PE loader overall. Aligning a given value and a given boundary is a relatively simple operation:

```cpp
template <typename T>
T align(T value, T alignment) {
   auto result = value + ((value % alignment == 0) ? 0 : alignment - (value % alignment));
   return result;
}
```

This function essentially pads a potentially unaligned value with the remainder needed to be properly aligned to a given boundary.

To properly add arbitrary data to our PE file, we need to be mindful of *file alignment* in particular-- we can calculate the proper aligned values for the PE file when it's in memory later, but for now when adding our data we need to align our file to the file alignment boundary. In the following code, we acquire the headers, then the file alignment and section alignment boundaries, then proceed to align our stub data to the file boundary and append our newly packed section.

```cpp
// next, load the stub and get some initial information
std::vector<std::uint8_t> stub_data = load_resource(MAKEINTRESOURCE(IDB_STUB), "STUB");
auto dos_header = reinterpret_cast<IMAGE_DOS_HEADER *>(stub_data.data());
auto e_lfanew = dos_header->e_lfanew;

// get the nt header and get the alignment information
auto nt_header = reinterpret_cast<IMAGE_NT_HEADERS64 *>(stub_data.data() + e_lfanew);
auto file_alignment = nt_header->OptionalHeader.FileAlignment;
auto section_alignment = nt_header->OptionalHeader.SectionAlignment;

// align the buffer to the file boundary if it isn't already
if (stub_data.size() % file_alignment != 0)
   stub_data.resize(align<std::size_t>(stub_data.size(), file_alignment));
      
// save the offset to our new section for later for our new PE section
auto raw_offset = static_cast<std::uint32_t>(stub_data.size());

// encode the size of our unpacked data into the stub data
auto unpacked_size = target.size();
stub_data.insert(stub_data.end(),
                 reinterpret_cast<std::uint8_t *>(&unpacked_size),
                 reinterpret_cast<std::uint8_t *>(&unpacked_size)+sizeof(std::size_t));

// add our compressed data.
stub_data.insert(stub_data.end(), packed.begin(), packed.end());
```

Now, we may have added the data for our section in accordance with the file section boundaries, but our stub executable is still not aware of this section in the PE file. We need to not only parse the *section table* of the PE file, but add a new entry that points at our section. This is the reason for the `raw_offset` variable.

First, we can increment the number of sections easily by updating the `NumberOfSections`. Typically, the data after the last section table is zeroed out, so we can overwrite the zeroed data with our new section easily.

```cpp
// increment the number of sections in the file header
auto section_index = nt_header->FileHeader.NumberOfSections;
++nt_header->FileHeader.NumberOfSections;
```

Next, we need to acquire a pointer to the section table itself. While it does technically immediately follow the optional header of the NT headers, the size of the optional header is actually determined by the NT file header's `SizeOfOptionalHeader` value. So, in order to get there, we need to calculate a pointer from the top of the `OptionalHeader` struct to the offset provided by the `SizeOfOptionalHeader` value.

```cpp
// acquire a pointer to the section table
auto size_of_header = nt_header->FileHeader.SizeOfOptionalHeader;
auto section_table = reinterpret_cast<IMAGE_SECTION_HEADER *>(
   reinterpret_cast<std::uint8_t *>(&nt_header->OptionalHeader)+size_of_header
);
```

Finally, we're ready to start adding our section metadata. This is our PE [section header](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_section_header):

```c
typedef struct _IMAGE_SECTION_HEADER {
    BYTE    Name[IMAGE_SIZEOF_SHORT_NAME]; // IMAGE_SIZEOF_SHORT_NAME is 8
    union {
            DWORD   PhysicalAddress;
            DWORD   VirtualSize;
    } Misc;
    DWORD   VirtualAddress;
    DWORD   SizeOfRawData;
    DWORD   PointerToRawData;
    DWORD   PointerToRelocations;
    DWORD   PointerToLinenumbers;
    WORD    NumberOfRelocations;
    WORD    NumberOfLinenumbers;
    DWORD   Characteristics;
} IMAGE_SECTION_HEADER, *PIMAGE_SECTION_HEADER;
```

The particular variables we're interested in for our new section header are `Name`, `VirtualSize`, `VirtualAddress`, `SizeOfRawData`, `PointerToRawData` and `Characteristics`. At this point, you should be aware that the reason you need to be aware of two types of alignment-- file alignment and memory alignment-- is because there's two different types of memory states for a given PE executable: what it looks like on *disk*, and what it looks like in *memory*, as a result of the loading process. It is possible to configure a given PE file to have the same memory layout regardless of being loaded or not, but it is not a frequent configuration. 

The `Name` variable is the 8-byte label you can give your new section. I've chosen `.packed`, as it's a 7-byte ASCII string and fits perfectly within the buffer.

`VirtualAddress` refers to the offset of a given section in memory. It is also known as a "relative virtual address," or RVA. `VirtualSize` refers to the size of the section in memory. (Of note, MSVC compiles this value as the unaligned size value of the section, so we follow this convention in our section as well.) `PointerToRawData` refers to the offset of a given section on disk, and `SizeOfRawData` refers to the size of the section on disk.

`Characteristics` is complicated and can refer to a section being readable, writable or executable, on top of other signifiers, see the characteristics section of [the `IMAGE_SECTION_HEADER` documentation](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_section_header). For now, you should know that all we need is for the section to be readable and marked as containing initialized data.

With all this in mind, we can now create our new section!

```cpp
// get a pointer to our new section and the previous section
auto section = &section_table[section_index];
auto prev_section = &section_table[section_index-1];

// calculate the memory offset, memory size and raw aligned size of our packed section
auto virtual_offset = align(prev_section->VirtualAddress + prev_section->Misc.VirtualSize, section_alignment);
auto virtual_size = section_size;
auto raw_size = align<DWORD>(section_size, file_alignment);

// assign the section metadata
std::memcpy(section->Name, ".packed", 8);
section->Misc.VirtualSize = virtual_size;
section->VirtualAddress = virtual_offset;
section->SizeOfRawData = raw_size;
section->PointerToRawData = raw_offset;

// mark our section as initialized, readable data.
section->Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA;
```

This now poses a small problem, though: the size of the image changed. You would think it *wouldn't* be a problem, but in the NT header's optional header is a variable called `SizeOfImage` that determines how much space the loader needs to allocate our executable. This is a simple fix, though: the size we need for our image is the size of the last section aligned to the section alignment boundary.

```cpp
// calculate the new size of the image.
nt_header->OptionalHeader.SizeOfImage = align(virtual_offset + virtual_size, section_alignment);
```

And that's it! We've successfully added our compressed binary as a new section for our stub to eventually decompress and load. Now we can simply save the modified stub image to disk.

```cpp
std::ofstream fp("packed.exe", std::ios::binary);
   
if (!fp.is_open()) {
   std::cerr << "Error: couldn't open packed binary for writing." << std::endl;
   ExitProcess(9);
}
   
fp.write(reinterpret_cast<const char *>(stub_data.data()), stub_data.size());
fp.close();
```

Congratulations! We've so far accomplished the following:

* Compiled, injected and retrieved our stub binary from the resource directory of our packer at runtime
* Parsed the executable headers of both our stub binary and our target binary to retrieve key information
* Modified an executable to extend its section data to contain our packed executable

We're half-way done with writing our packer! Now we can move on to arguably the hardest part of the process: fleshing out the stub binary.

## Simulating the loader

While the minutae of writing an unpack stub can get complicated under the hood, it fundamentally just comes down to just a few steps:

* **Retrieve the image data**: acquire and decompress (and optionally deobfuscate) the target binary representation for further processing.
* **Load the image**: by far the most complicated, this is where the loader is simulated and the target image prepared for execution.
* **Call the entrypoint of the image**: typically called "original entry point," or OEP, this is the point by which you transfer from the loading stage to the execution stage for your packed binary.

This process is so simple, our main routine is just a handful functions:

```cpp
int main(int argc, char *argv[]) {
   // first, decompress the image from our added section
   auto image = get_image();
   
   // next, prepare the image to be a virtual image   
   auto loaded_image = load_image(image);

   // resolve the imports from the executable
   load_imports(loaded_image);

   // relocate the executable
   relocate(loaded_image);

   // get the headers from our loaded image
   auto nt_headers = get_nt_headers(loaded_image);

   // acquire and call the entrypoint
   auto entrypoint = loaded_image + nt_headers->OptionalHeader.AddressOfEntryPoint;
   reinterpret_cast<void(*)()>(entrypoint)();
   
   return 0;
}
```

### Reading our PE from memory

First, we need to somehow get the data from our packer-created section from the running binary. Is it possible to get the headers of the running binary at runtime? Yes, absolutely! [`GetModuleHandleA`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandlea), with a null argument, ultimately returns a pointer to our running PE headers! So at runtime, you have easy access to the image as it exists in memory. This is what makes adding a new section to our binary so appealing: we can very easily parse our target section from the binary.

With what we've learned about parsing the section table, this section of code should be easy to understand:

```cpp
// find our packed section
auto base = reinterpret_cast<const std::uint8_t *>(GetModuleHandleA(NULL));
auto nt_header = get_nt_headers(base);
auto section_table = reinterpret_cast<const IMAGE_SECTION_HEADER *>(
   reinterpret_cast<const std::uint8_t *>(&nt_header->OptionalHeader)+nt_header->FileHeader.SizeOfOptionalHeader
);
const IMAGE_SECTION_HEADER *packed_section = nullptr;

for (std::uint16_t i=0; i<nt_header->FileHeader.NumberOfSections; ++i)
{
   if (std::memcmp(section_table[i].Name, ".packed", 8) == 0)
   {
      packed_section = &section_table[i];
      break;
   }
}

if (packed_section == nullptr) {
   std::cerr << "Error: couldn't find packed section in binary." << std::endl;
   ExitProcess(1);
}
```

Next, we need to decompress our stub data from the binary. zlib recommends we encode the size of the original decompressed payload reachable in some way to the decompression routine, which is why we encoded the size of the decompressed binary at the header of our packed data. So, we get a pointer to our decompressed data, create a new buffer that can hold the decompressed data, and proceed to call zlib's decompression function.

```cpp
// decompress our packed image
auto section_start = base + packed_section->VirtualAddress;
auto section_end = section_start + packed_section->Misc.VirtualSize;
auto unpacked_size = *reinterpret_cast<const std::size_t *>(section_start);
auto packed_data = section_start + sizeof(std::size_t);
auto packed_size = packed_section->Misc.VirtualSize - sizeof(std::size_t);

auto decompressed = std::vector<std::uint8_t>(unpacked_size);
uLong decompressed_size = static_cast<uLong>(unpacked_size);

if (uncompress(decompressed.data(), &decompressed_size, packed_data, packed_size) != Z_OK)
{
   std::cerr << "Error: couldn't decompress image data." << std::endl;
   ExitProcess(2);
}
                  
return decompressed;
```

As you can see, `get_image` was a relatively simple function at the end of the day. We got our target binary extracted from the appended section, so now we need to load it.

### Loading our PE for execution

The Windows executable loader does a lot of different things under the hood, and supports a lot of different executable configurations. You are likely to encounter a variety of errors with the packer this tutorial produces if you explore more than the example binary, as the configuration we'll be building today is technically very minimal. But for the sake of establishing that bare minimum to execution, we need to do the following things to load a modern Windows executable:

* allocate the image that holds the memory representation of the executable image
* map our executable image's sections, including the headers, to that allocated image
* resolve the runtime imports to other libraries that our binary needs
* remap the binary image so that a variety of addresses in our image point where they're supposed to

Let's start with the `load_image` function. For starters, we need to acquire our section table from the packed binary. This will eventually be mapped onto our newly allocated image. For a proper executable buffer, allocation is very simple-- take the `SizeOfImage` value from our section headers and create a new buffer with [`VirtualAlloc`](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc) that creates a readable, writable and executable image for us to map to.

```cpp
// get the original image section table
auto nt_header = get_nt_headers(image.data());
auto section_table = reinterpret_cast<const IMAGE_SECTION_HEADER *>(
   reinterpret_cast<const std::uint8_t *>(&nt_header->OptionalHeader)+nt_header->FileHeader.SizeOfOptionalHeader
);

// create a new VirtualAlloc'd buffer with read, write and execute privileges
// that will fit our image
auto image_size = nt_header->OptionalHeader.SizeOfImage;
auto base = reinterpret_cast<std::uint8_t *>(VirtualAlloc(nullptr,
                                                          image_size,
                                                          MEM_COMMIT | MEM_RESERVE,
                                                          PAGE_EXECUTE_READWRITE));

if (base == nullptr) {
   std::cerr << "Error: VirtualAlloc failed: Windows error " << GetLastError() << std::endl;
   ExitProcess(3);
}
```

With our buffer allocated, what we need to do is copy the headers and the sections to it. This needs to conform with the `SectionAlignment` variable from earlier. Luckily, the way we prepared our section-- and the way the other sections are prepared-- they're already aligned to the `SectionAlignment` boundary. Suffice to say, the end-result is simple pointer arithmetic: copy our target image at the `PointerToRawData` offset, into our loaded image at the `VirtualAddress` offset.

Optionally, you can copy the PE headers to the top of the image. It eases development to retain the original headers, but wanting to remove them is a good step toward building an analysis-hostile packer. We copy the headers here for the sake of ease of use.

```cpp
// copy the headers to our new virtually allocated image
std::memcpy(base, image.data(), nt_header->OptionalHeader.SizeOfHeaders);

// copy our sections to their given addresses in the virtual image
for (std::uint16_t i=0; i<nt_header->FileHeader.NumberOfSections; ++i)
   if (section_table[i].SizeOfRawData > 0)
      std::memcpy(base+section_table[i].VirtualAddress,
                  image.data()+section_table[i].PointerToRawData,
                  section_table[i].SizeOfRawData);

return base;
```

So the easy part of the loading process is over: we've retrieved our binary from memory, unpacked it, and remapped its sections to an executable memory region. With our image prepared, we can delve into the nitty gritty of the loading process.

### Resolving API imports

Within the optional headers is something called a *data directory*. This directory contains a lot of different information about the executable, such as symbols exported by the image and resources such as icons and bitmaps. We're going to be parsing two data directories in this tutorial: the **import directory** and the **relocation directory**. Each directory is hardcoded, and their indexes can be found [in the documentation for the optional header](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_optional_header32) (scroll down to the description of `DataDirectory`). A data directory is present if its `VirtualAddress` value is non-null.

```c
typedef struct _IMAGE_DATA_DIRECTORY {
    DWORD   VirtualAddress;
    DWORD   Size;
} IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;
```

We retrieve pointers to data directories through casting the RVA provided. For example, this is how you eventually get the import table from the import data directory:

```cpp
// get the import table directory entry
auto nt_header = get_nt_headers(image);
auto directory_entry = nt_header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

// if there are no imports, that's fine-- return because there's nothing to do.
if (directory_entry.VirtualAddress == 0) { return; }

// get a pointer to the import descriptor array
auto import_table = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(image + directory_entry.VirtualAddress);
```

To resolve API imports, the loader parses this directory, then proceeds to load necessary libraries and acquire their imported functions. It is luckily a relatively easy directory to parse.

It starts with an import descriptor structure:

```c
typedef struct _IMAGE_IMPORT_DESCRIPTOR {
    union {
        DWORD   Characteristics;            // 0 for terminating null import descriptor
        DWORD   OriginalFirstThunk;         // RVA to original unbound IAT (PIMAGE_THUNK_DATA)
    } DUMMYUNIONNAME;
    DWORD   TimeDateStamp;                  // 0 if not bound,
                                            // -1 if bound, and real date\time stamp
                                            //     in IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT (new BIND)
                                            // O.W. date/time stamp of DLL bound to (Old BIND)

    DWORD   ForwarderChain;                 // -1 if no forwarders
    DWORD   Name;
    DWORD   FirstThunk;                     // RVA to IAT (if bound this IAT has actual addresses)
} IMAGE_IMPORT_DESCRIPTOR;
```

What we're most concerned about are two somewhat confusingly named variables: `OriginalFirstThunk` and `FirstThunk`. `OriginalFirstThunk` contains information about the imports desired by this executable as it relates to the DLL given by the `Name` RVA. To add to the confusion, so does `FirstThunk`. What differentiates them? `FirstThunk` contains the imports *after they've been resolved*. This resolution is performed by the infamous [`GetProcAddress`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress) function.

Our thunks are additional data structures to contend with:

```c
typedef struct _IMAGE_THUNK_DATA64 {
    union {
        ULONGLONG ForwarderString;  // PBYTE 
        ULONGLONG Function;         // PDWORD
        ULONGLONG Ordinal;
        ULONGLONG AddressOfData;    // PIMAGE_IMPORT_BY_NAME
    } u1;
} IMAGE_THUNK_DATA64;
```

This data structure covers import as well as export thunks, so `ForwarderString` can be ignored. An import thunk can be either an `Ordinal` or an RVA to another structure, `IMAGE_IMPORT_BY_NAME`. An *ordinal* is simply an offset into the export table of the given DLL. An `IMAGE_IMPORT_BY_NAME` structure looks like this:

```c
typedef struct _IMAGE_IMPORT_BY_NAME {
    WORD    Hint;
    CHAR   Name[1];
} IMAGE_IMPORT_BY_NAME, *PIMAGE_IMPORT_BY_NAME;
```

This is an example of a *variadic structure*. It takes advantage of a lack of bounds checking in array access in order to create structures that can be variable in size. `Name`, in this case, is expected to be a zero-terminated C-string.

Because binary data doesn't contain types, an *ordinal* and an *import* are differentiated by the most significant bit in the thunk entry. The ordinal is contained in the lower half of the integer on both 32-bit and 64-bit implementations.

Our import table works like a C-string-- its last entry is a null terminated `OriginalFirstThunk` to signify the end of the potential imports. Our thunk data works the same way, terminating by a null entry in the thunk array.

To put everything together, this is how we approach parsing the import table in pseudocode:

```
for every import descriptor:
    load the dll
    parse the original and first thunk
    
    for every thunk:
        if ordinal bit set:
            import by ordinal
        else:
            import by name
            
        store import in first thunk
```

Interestingly, despite `GetProcAddress` being typed with a C-string as the argument to the function, Windows expects you to import by ordinal by simply casting the ordinal value as a C-string. Go figure.

With these explanations in mind, this while loop should now make sense:

```cpp
// when we reach an OriginalFirstThunk value that is zero, that marks the end of our array.
// typically all values in the import descriptor are zero, but we do this
// to be shorter about it.
while (import_table->OriginalFirstThunk != 0)
{
   // get a string pointer to the DLL to load.
   auto dll_name = reinterpret_cast<char *>(image + import_table->Name);

   // load the DLL with our import.
   auto dll_import = LoadLibraryA(dll_name);

   if (dll_import == nullptr) {
      std::cerr << "Error: failed to load DLL from import table: " << dll_name << std::endl;
      ExitProcess(4);
   }

   // load the array which contains our import entries
   auto lookup_table = reinterpret_cast<IMAGE_THUNK_DATA64 *>(image + import_table->OriginalFirstThunk);

   // load the array which will contain our resolved imports
   auto address_table = reinterpret_cast<IMAGE_THUNK_DATA64 *>(image + import_table->FirstThunk);

   // an import can be one of two things: an "import by name," or an "import ordinal," which is
   // an index into the export table of a given DLL.
   while (lookup_table->u1.AddressOfData != 0)
   {
      FARPROC function = nullptr;
      auto lookup_address = lookup_table->u1.AddressOfData;

      // if the top-most bit is set, this is a function ordinal.
      // otherwise, it's an import by name.
      if (lookup_address & IMAGE_ORDINAL_FLAG64 != 0)
      {
         // get the function ordinal by masking the lower 32-bits of the lookup address.
         function = GetProcAddress(dll_import,
                                   reinterpret_cast<LPSTR>(lookup_address & 0xFFFFFFFF));

         if (function == nullptr) {
            std::cerr << "Error: failed ordinal lookup for " << dll_name << ": " << (lookup_address & 0xFFFFFFFF) << std::endl;
            ExitProcess(5);
         }
      }
      else {
         // in an import by name, the lookup address is an offset to
         // an IMAGE_IMPORT_BY_NAME structure, which contains our function name
         // to import
         auto import_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(image + lookup_address);
         function = GetProcAddress(dll_import, import_name->Name);

         if (function == nullptr) {
            std::cerr << "Error: failed named lookup: " << dll_name << "!" << import_name->Name << std::endl;
            ExitProcess(6);
         }
      }

      // store either the ordinal function or named function
      // in our address table.
      address_table->u1.Function = reinterpret_cast<std::uint64_t>(function);

      // advance to the next entries in the address table and lookup table
      ++lookup_table;
      ++address_table;
   }

   // advance to the next entry in our import table
   ++import_table;
}
```

With imports resolved, we can now move onto the final piece: dealing with the relocation directory!

### Resolving addresses

The next data directory to contend with is what's called the *relocation directory*. This directory is responsible for translating absolute addresses within the code into their new base values. This process essentially implements something you might be familiar with called [address space layout randomization](https://en.wikipedia.org/wiki/Address_space_layout_randomization), but is also responsible for making sure DLL address spaces don't collide with one another.

First, we need to make sure that our binary is actually capable of moving address bases. Sometimes, for older binaries especially, this is not enabled. Within the characteristics of a given Windows executable are its characteristics, confusingly referred to as `DllCharacteristics` in the optional header. We're concerned with the "dynamic base" characteristic. There's a special way to unpack non-ASLR binaries, which we're not covering, so it's an error if our binary doesn't support it. (This is better as an error in your packer, not your stub, but for the sake of ease of flow of PE header education, it was placed here instead.)

```cpp
// first, check if we can even relocate the image. if the dynamic base flag isn't set,
// then this image probably isn't prepared for relocating.
auto nt_header = get_nt_headers(image);

if (nt_header->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE == 0)
{
   std::cerr << "Error: image cannot be relocated." << std::endl;
   ExitProcess(7);
}
    
// once we know we can relocate the image, make sure a relocation directory is present
auto directory_entry = nt_header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

if (directory_entry.VirtualAddress == 0) {
   std::cerr << "Error: image can be relocated, but contains no relocation directory." << std::endl;
   ExitProcess(8);
}
```

Next, we need to calculate the *address delta*. This is simply the difference between the image's `ImageBase` variable and the virtual image's base address. It is used to quickly adjust the values of hardcoded addresses in the binary.

```cpp
// calculate the difference between the image base in the compiled image
// and the current virtually allocated image. this will be added to our
// relocations later.
std::uintptr_t delta = reinterpret_cast<std::uintptr_t>(image) - nt_header->OptionalHeader.ImageBase;
```

Now we're ready to tackle the relocation table.

```c
typedef struct _IMAGE_BASE_RELOCATION {
    DWORD   VirtualAddress;
    DWORD   SizeOfBlock;
//  WORD    TypeOffset[1];
} IMAGE_BASE_RELOCATION;
```

Note the commented out `TypeOffset` array, it's actually relevant here! A relocation table consists of blocks of offsets containing addresses to adjust, identified by the `VirtualAddress` RVA. The `TypeOffset` array contains encoded word values which contain the relocation type as well as the offset from `VirtualAddress` to adjust. As far as relocation type goes, for 64-bit binaries, we're only concerned with one relocation type. Unfortunately, this structure is not actually a variadic struct, so we have to do some pointer arithmetic to get the `TypeOffset` array.

As mentioned, the `TypeOffset` array contains encoded words. The upper 4 bits (mask `0xF000`) contain the relocation type, which can be found in the ["base relocation types" section](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format) of the PE format documentation. The lower 12 bits (mask `0x0FFF`) contain the offset from the `VirtualAddress` argument to adjust.

Explaining it is a chore and just comes off as very confusing in the end. Getting a pointer to an address to relocate looks like this:

```cpp
auto ptr = reinterpret_cast<std::uintptr_t *>(image + relocation_table->VirtualAddress + offset);
```

And adjusting that address looks like this:

```cpp
*ptr += delta;
```

So as complicated as it is to explain the relocation directory, the operations are actually very simple to understand in code.

Advancing to the next block of relocation data is made easy by the `SizeOfBlock` variable. This block contains the size of our header *as well as* the size of the `TypeOffset` array. If `TypeOffset` were instead a staticly sized array, we would simply advance to the next relocation entry by adding the call to `sizeof` of the relocation header.

With all this explained, you should be able to understand this relocation code:

```cpp
// get the relocation table.
auto relocation_table = reinterpret_cast<IMAGE_BASE_RELOCATION *>(image + directory_entry.VirtualAddress);

// when the virtual address for our relocation header is null,
// we've reached the end of the relocation table.
while (relocation_table->VirtualAddress != 0)
{
   // since the SizeOfBlock value also contains the size of the relocation table header,
   // we can calculate the size of the relocation array by subtracting the size of
   // the header from the SizeOfBlock value and dividing it by its base type: a 16-bit integer.
   std::size_t relocations = (relocation_table->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(std::uint16_t);

   // additionally, the relocation array for this table entry is directly after
   // the relocation header
   auto relocation_data = reinterpret_cast<std::uint16_t *>(&relocation_table[1]);

   for (std::size_t i=0; i<relocations; ++i)
   {
      // a relocation is an encoded 16-bit value:
      //   * the upper 4 bits are its relocation type
      //     (https://learn.microsoft.com/en-us/windows/win32/debug/pe-format see "base relocation types")
      //   * the lower 12 bits contain the offset into the relocation entry's address base into the image
      //
      auto relocation = relocation_data[i];
      std::uint16_t type = relocation >> 12;
      std::uint16_t offset = relocation & 0xFFF;
      auto ptr = reinterpret_cast<std::uintptr_t *>(image + relocation_table->VirtualAddress + offset);

      // there are typically only two types of relocations for a 64-bit binary:
      //   * IMAGE_REL_BASED_DIR64: a 64-bit delta calculation
      //   * IMAGE_REL_BASED_ABSOLUTE: a no-op
      //
      if (type == IMAGE_REL_BASED_DIR64)
         *ptr += delta;
   }

   // the next relocation entry is at SizeOfBlock bytes after the current entry
   relocation_table = reinterpret_cast<IMAGE_BASE_RELOCATION *>(
      reinterpret_cast<std::uint8_t *>(relocation_table) + relocation_table->SizeOfBlock
   );
}
```

Congratulations! Our image is now ready for execution! We've accomplished a lot so far:

* we unpacked our binary from our section table at runtime
* we mapped our binary onto an executable memory region
* we resolved the imports needed for our binary to execute
* we relocated the addresses in the image to point to our new image base

Now we're ready to run our unpacked binary!

### Transferring execution

Let's go back to our main loop now:

```cpp
int main(int argc, char *argv[]) {
   // first, decompress the image from our added section
   auto image = get_image();
   
   // next, prepare the image to be a virtual image   
   auto loaded_image = load_image(image);

   // resolve the imports from the executable
   load_imports(loaded_image);

   // relocate the executable
   relocate(loaded_image);

   // get the headers from our loaded image
   auto nt_headers = get_nt_headers(loaded_image);

   // acquire and call the entrypoint
   auto entrypoint = loaded_image + nt_headers->OptionalHeader.AddressOfEntryPoint;
   reinterpret_cast<void(*)()>(entrypoint)();
   
   return 0;
}
```

As you can see, transferring execution is very simple, although does require knowledge of [function pointers](https://en.wikipedia.org/wiki/Function_pointer). These are our relevant bits:

```cpp
// acquire and call the entrypoint
auto entrypoint = loaded_image + nt_headers->OptionalHeader.AddressOfEntryPoint;
reinterpret_cast<void(*)()>(entrypoint)();
```

`AddressOfEntryPoint` is, as you can guess, an RVA in our loaded image's code entry. Despite what you may know about `main` entrypoints, the raw entrypoint of a given binary is untyped-- your C++ compiler is mainly responsible for setting up the code environment to feed arguments to your expected main functions, whether they be `main` or `WinMain`.

Fundamentally, this is how a function pointer is declared:

```c
return_type (*variable_name)(int arg1, int arg2, ...)
```

Hence, our function pointer to call our entry point-- having no type associated with it-- looks like this:

```c
void (*entrypoint)()
```

As a cast, it reduces to this:

```c
void(*)()
```

Putting everything together, we can simply cast our entrypoint as a function pointer and call it in the same line, like so:

```cpp
reinterpret_cast<void(*)()>(entrypoint)();
```

With everything loaded correctly, you should see your packed program run. In our case, since we packed our dummy executable, it simply outputs a message:

```
$ ./packed.exe
I'm just a little guy!
```

Congratulations! You're done! You've just written a Windows packer!

## Further exercises

* **Attack the analyst**: Learn to implement [some anti-debug techniques](https://anti-reversing.com/Downloads/Anti-Reversing/The_Ultimate_Anti-Reversing_Reference.pdf) and harden your packer.
* **Expand your support**: Learn to implement a non-relocatable binary, or learn to implement further directories such as the thread-local storage directory (`IMAGE_TLS_DIRECTORY`) and the resource directory (`IMAGE_RESOURCE_DIRECTORY`). Since documentation is lacking at this level, you can see my implementations in [exe-rs](https://github.com/exe-rs).
* **Obfuscate your stub**: Try to figure out how the analyst is going to unpack your binary and prevent ease of unpacking, such as [erasing the headers](https://github.com/frank2/packer-tutorial/blob/main/stub/src/main.cpp#L84).
