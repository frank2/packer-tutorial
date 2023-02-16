#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>
#include <zlib.h>

#include "stub.hpp"

std::vector<std::uint8_t> read_file(const std::string &filename) {
   std::ifstream fp(filename, std::ios::binary);
   
   if (!fp.is_open()) {
      std::cerr << "Error: couldn't open file: " << filename << std::endl;
      ExitProcess(2);
   }

   auto vec_data = std::vector<std::uint8_t>();
   vec_data.insert(vec_data.end(),
                   std::istreambuf_iterator<char>(fp),
                   std::istreambuf_iterator<char>());

   return vec_data;
}

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

template <typename T>
T align(T value, T alignment) {
   auto result = value + ((value % alignment == 0) ? 0 : alignment - (value % alignment));
   return result;
}

int main(int argc, char *argv[]) {
   if (argc != 2)
   {
      std::cerr << "Error: no file to pack!" << std::endl;
      ExitProcess(1);
   }

   // read the file to pack
   auto target = read_file(argv[1]);

   // validate that this is a PE file we can pack
   validate_target(target);

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

   // calculate the section size for storage in the PE file
   auto section_size = static_cast<std::uint32_t>(packed.size() + sizeof(std::size_t));

   // re-acquire an NT header pointer since our buffer likely changed addresses
   // from the resize
   nt_header = reinterpret_cast<IMAGE_NT_HEADERS64 *>(stub_data.data() + e_lfanew);

   // pad the section data with 0s if we aren't on the file alignment boundary
   if (stub_data.size() % file_alignment != 0)
      stub_data.resize(align<std::size_t>(stub_data.size(), file_alignment));

   // increment the number of sections in the file header
   auto section_index = nt_header->FileHeader.NumberOfSections;
   ++nt_header->FileHeader.NumberOfSections;

   // acquire a pointer to the section table
   auto size_of_header = nt_header->FileHeader.SizeOfOptionalHeader;
   auto section_table = reinterpret_cast<IMAGE_SECTION_HEADER *>(
      reinterpret_cast<std::uint8_t *>(&nt_header->OptionalHeader)+size_of_header
   );

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

   // calculate the new size of the image.
   nt_header->OptionalHeader.SizeOfImage = align(virtual_offset + virtual_size, section_alignment);

   std::ofstream fp("packed.exe", std::ios::binary);
   
   if (!fp.is_open()) {
      std::cerr << "Error: couldn't open packed binary for writing." << std::endl;
      ExitProcess(9);
   }
   
   fp.write(reinterpret_cast<const char *>(stub_data.data()), stub_data.size());
   fp.close();

   std::cout << "File successfully packed." << std::endl;
   
   return 0;
}
