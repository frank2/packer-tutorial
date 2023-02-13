#include <cstring>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <vector>
#include <windows.h>
#include <zlib.h>

IMAGE_NT_HEADERS64 *get_nt_headers(std::uint8_t *image) {
   auto dos_header = reinterpret_cast<IMAGE_DOS_HEADER *>(image);
   return reinterpret_cast<IMAGE_NT_HEADERS64 *>(image + dos_header->e_lfanew);
}

const IMAGE_NT_HEADERS64 *get_nt_headers(const std::uint8_t *image) {
   auto dos_header = reinterpret_cast<const IMAGE_DOS_HEADER *>(image);
   return reinterpret_cast<const IMAGE_NT_HEADERS64 *>(image + dos_header->e_lfanew);
}

std::vector<std::uint8_t> get_image()
{
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
}

std::uint8_t *load_image(const std::vector<std::uint8_t> &image) {
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

   // copy the headers to our new virtually allocated image
   std::memcpy(base, image.data(), nt_header->OptionalHeader.SizeOfHeaders);

   // copy our sections to their given addresses in the virtual image
   for (std::uint16_t i=0; i<nt_header->FileHeader.NumberOfSections; ++i)
      if (section_table[i].SizeOfRawData > 0)
         std::memcpy(base+section_table[i].VirtualAddress,
                     image.data()+section_table[i].PointerToRawData,
                     section_table[i].SizeOfRawData);

   return base;
}

void load_imports(std::uint8_t *image) {
   // get the import table directory entry
   auto nt_header = get_nt_headers(image);
   auto directory_entry = nt_header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

   // if there are no imports, that's fine-- return because there's nothing to do.
   if (directory_entry.VirtualAddress == 0) { return; }

   // get a pointer to the import descriptor array
   auto import_table = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(image + directory_entry.VirtualAddress);

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
}

void relocate(std::uint8_t *image) {
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

   // calculate the difference between the image base in the compiled image
   // and the current virtually allocated image. this will be added to our
   // relocations later.
   std::uintptr_t delta = reinterpret_cast<std::uintptr_t>(image) - nt_header->OptionalHeader.ImageBase;

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
}
   
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
