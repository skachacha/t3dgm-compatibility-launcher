#include "shared.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
struct Reader {
    std::shared_ptr<void> handle;
    std::uint64_t size = 0;

    explicit Reader(const fs::path& path) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        win32_need(file != INVALID_HANDLE_VALUE, "Cannot read executable or DLL");
        handle = std::shared_ptr<void>(file, [](void* value) { CloseHandle(value); });
        BY_HANDLE_FILE_INFORMATION information{};
        win32_need(GetFileInformationByHandle(file, &information), "Cannot inspect executable or DLL");
        need(!(information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)),
             "Executable or DLL is not a regular file");
        size = (std::uint64_t(information.nFileSizeHigh) << 32) | information.nFileSizeLow;
    }

    void read(std::uint64_t offset, void* data, std::size_t count) const {
        need(offset <= size && count <= size-offset && count <= MAXDWORD, "Invalid PE file bounds");
        LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
        win32_need(SetFilePointerEx(handle.get(), position, nullptr, FILE_BEGIN), "Cannot read PE file");
        DWORD actual = 0;
        win32_need(ReadFile(handle.get(), data, DWORD(count), &actual, nullptr), "Cannot read PE file");
        need(actual == count, "Truncated PE file");
    }

    template<class T> T object(std::uint64_t offset) const {
        T value{}; read(offset, &value, sizeof value); return value;
    }
};

struct Image {
    IMAGE_FILE_HEADER header{};
    IMAGE_OPTIONAL_HEADER32 optional{};
    std::vector<IMAGE_SECTION_HEADER> sections;

    explicit Image(const Reader& input) {
        auto dos = input.object<IMAGE_DOS_HEADER>(0);
        need(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= LONG(sizeof dos) && dos.e_lfanew <= 1048576,
             "Not a supported Windows executable");
        auto pe = std::uint64_t(dos.e_lfanew);
        need(input.object<DWORD>(pe) == IMAGE_NT_SIGNATURE, "Not a supported PE file");
        header = input.object<IMAGE_FILE_HEADER>(pe+4);
        need(header.Machine == IMAGE_FILE_MACHINE_I386 && header.NumberOfSections >= 1 &&
             header.NumberOfSections <= 32 && header.SizeOfOptionalHeader >= sizeof optional,
             "A 32-bit x86 executable or DLL is required");
        optional = input.object<IMAGE_OPTIONAL_HEADER32>(pe+4+sizeof header);
        need(optional.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC, "A PE32 image is required");
        auto start = pe+4+sizeof header+header.SizeOfOptionalHeader;
        for (unsigned i=0; i<header.NumberOfSections; ++i) {
            auto section = input.object<IMAGE_SECTION_HEADER>(start+i*sizeof(IMAGE_SECTION_HEADER));
            need(std::uint64_t(section.PointerToRawData)+section.SizeOfRawData <= input.size,
                 "PE section extends outside the file");
            need(std::uint64_t(section.VirtualAddress)+std::max(section.Misc.VirtualSize,section.SizeOfRawData)
                     <= optional.SizeOfImage, "PE section extends outside the image");
            for (const auto& previous : sections) {
                auto overlaps = [](std::uint64_t a, std::uint64_t n, std::uint64_t b, std::uint64_t m) {
                    return n && m && a < b+m && b < a+n;
                };
                need(!overlaps(section.VirtualAddress,std::max(section.Misc.VirtualSize,section.SizeOfRawData),
                               previous.VirtualAddress,std::max(previous.Misc.VirtualSize,previous.SizeOfRawData)) &&
                     !overlaps(section.PointerToRawData,section.SizeOfRawData,
                               previous.PointerToRawData,previous.SizeOfRawData), "Overlapping PE sections");
            }
            sections.push_back(section);
        }
    }
};

bool section_name(const IMAGE_SECTION_HEADER& section, const char* name) {
    char padded[8]{};
    auto size = std::strlen(name);
    return size <= sizeof padded && (std::memcpy(padded,name,size), std::memcmp(section.Name,padded,8)==0);
}
}

fs::path default_executable(const fs::path& here) {
    for (const auto& folder : {here,here.parent_path()})
        if (fs::is_regular_file(folder/L"t3dgm.exe")) return folder/L"t3dgm.exe";
    throw std::runtime_error("Drop a game EXE onto the launcher, specify its filename, or place the launcher beside t3dgm.exe");
}

Target identify_target(const fs::path& filename) {
    auto path = fs::canonical(filename);
    need(_wcsicmp(path.extension().c_str(),L".exe")==0, "Select an EXE file");
    Reader input(path);
    Image image(input);
    need(!(image.header.Characteristics & IMAGE_FILE_DLL) &&
         (image.header.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) &&
         image.optional.ImageBase == PreferredBase, "Unsupported executable image");
    unsigned executable_sections=0;
    for (const auto& section : image.sections)
        if (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) ++executable_sections;
    need(executable_sections==1,"Unsupported native runtime layout");
    for (const auto& profile : RuntimeProfiles) {
        if (image.optional.AddressOfEntryPoint != profile.entry_rva) continue;
        bool match=true;
        for (const auto& fingerprint : profile.sections) {
            auto found = std::find_if(image.sections.begin(),image.sections.end(),[&](const auto& section) {
                return section_name(section,fingerprint.name);
            });
            if (found==image.sections.end() || found->VirtualAddress!=fingerprint.rva ||
                found->Misc.VirtualSize!=fingerprint.virtual_size || found->SizeOfRawData!=fingerprint.raw_size ||
                found->PointerToRawData!=fingerprint.raw_offset || found->Characteristics!=fingerprint.flags ||
                hash_range(input.handle.get(),fingerprint.raw_offset,fingerprint.raw_size)!=fingerprint.sha256) {
                match=false; break;
            }
        }
        if (match) {
            // Hold a read-only file lease through process creation and shutdown.
            return Target{path,&profile,input.handle};
        }
    }
    throw std::runtime_error("This executable's native runtime has not been verified");
}

void validate_dependency(const Target& target) {
    auto path = target.folder()/L"d3drm.dll";
    need(fs::is_regular_file(path), "Include d3drm.dll beside the selected EXE");
    Reader input(path);
    Image image(input);
    need(image.header.Characteristics & IMAGE_FILE_DLL, "d3drm.dll is not a 32-bit DLL");
}
