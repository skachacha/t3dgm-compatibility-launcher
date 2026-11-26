#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

constexpr wchar_t WrapperSha256[] = L"94895cf6ede8fd0f74ee1447fdc08b7044a9c65a1a25a49a4df893a0370975a3";
constexpr wchar_t OriginalDDrawSha256[] = L"d977bddd66be64b23c60001d88f779e784e0fb48694147103ac57cf008247282";
constexpr wchar_t OriginalD3DImmSha256[] = L"8339438c623662415851062723a4e5141717ff7aa54ab41eb0774c7431db68ad";
constexpr wchar_t DevtestDDrawSha256[] = L"f68cd82bfb585c9b6676c6282b0fac713c9567eae624bc991c5bcfe5881c7bef";
constexpr wchar_t DevtestD3DImmSha256[] = L"85aa1d15d6880dbe927458f6a045a631264ba128528010b7296ec5c3a640e0ac";
constexpr std::uintptr_t PreferredBase = 0x400000;

enum class TemporaryRoot { WindowsTemp, DriveDbTemp };
struct SectionFingerprint {
    const char* name;
    std::uint32_t rva, virtual_size, raw_size, raw_offset, flags;
    const wchar_t* sha256;
};
struct RuntimeProfile {
    const wchar_t* name;
    std::uint32_t entry_rva, checkpoint_rva, temporary_rva;
    std::array<unsigned char, 9> signature;
    std::size_t signature_size;
    TemporaryRoot temporary_root;
    std::array<SectionFingerprint, 3> sections;
};

// Native code/data identity; artwork and game payloads are excluded.
inline constexpr std::array<RuntimeProfile, 5> RuntimeProfiles{{
    {L"Original / TGC", 0x117147, 0x4d967, 0x3224fc,
        {0x83, 0xc4, 0x4, 0x8d, 0x85, 0xb8, 0xfd, 0xff, 0xff}, 9, TemporaryRoot::WindowsTemp, {{
        {".text", 0x1000, 0x147c0e, 0x148000, 0x1000, 0x60000020, L"b11319eeb0656a706135206cb5ca84154d891a0174884404a1a7f535176e1818"},
        {".rdata", 0x149000, 0x20dc2, 0x21000, 0x149000, 0x40000040, L"c7ce4384dd0772a960b8e5cae37ab0ebd546d12efca385889df2dbc63d418569"},
        {".data", 0x16a000, 0x41ddc8, 0x12000, 0x16a000, 0xc0000040, L"12131b6904b45b176a4d60a91febe79b161e4b7b8cab8de5edc64d79cf49f096"}
    }}},
    {L"Steam", 0xb8b57, 0x36a6d, 0x2c1b64,
        {0x8d, 0x44, 0x24, 0x18}, 4, TemporaryRoot::WindowsTemp, {{
        {".text", 0x1000, 0xe94ae, 0xea000, 0x1000, 0x60000020, L"2cd920f481d646aa8ecde6b0974c246cb5b9d79c422b98859b9693065430d8fa"},
        {".rdata", 0xeb000, 0x200bc, 0x21000, 0xeb000, 0x40000040, L"27c716b64b401760c16bf8f7e6cd77f1a3c724f319934abcf6fd2fd6fa058126"},
        {".data", 0x10c000, 0x41b008, 0x10000, 0x10c000, 0xc0000040, L"a7396dfbf1540733df4cd7d9e4f527a201a8c2cc4e960b713a93d46c4cb759ce"}
    }}},
    {L"Vista standalone", 0x12d077, 0x4e1cd, 0x3383fc,
        {0x83, 0xc4, 0x4, 0x8d, 0x85, 0xb8, 0xfd, 0xff, 0xff}, 9, TemporaryRoot::DriveDbTemp, {{
        {".text", 0x1000, 0x15da2e, 0x15e000, 0x1000, 0x60000020, L"bf2715a5108208e120a0a0906d20e8489d24fcb336d9119f8460939dc3e2bb92"},
        {".rdata", 0x15f000, 0x20d2c, 0x21000, 0x15f000, 0x40000040, L"8e81069493100139baa8eba33663d76bc9b593b10236df1a1d1ce4fe557ced6e"},
        {".data", 0x180000, 0x41dd68, 0x12000, 0x180000, 0xc0000040, L"3b1fb3296f0ab2e7c2cc279d636258d05499e589b42718953badeb0b034c02ab"}
    }}},
    {L"September 2001 editor", 0x10c397, 0x497ab, 0x316cf4,
        {0x83, 0xc4, 0x4, 0x8d, 0x85, 0xb8, 0xfd, 0xff, 0xff}, 9, TemporaryRoot::WindowsTemp, {{
        {".text", 0x1000, 0x13ccea, 0x13d000, 0x1000, 0x60000020, L"bf58dbc4056de3c6e098d04d1de9dc7652f3471626793b45257a5dfabb6ae0d5"},
        {".rdata", 0x13e000, 0x20ba8, 0x21000, 0x13e000, 0x40000040, L"be93fffe980c6522218a742d5882aa9d12c91eeca02bdfd616e40e2c2d19bac7"},
        {".data", 0x15f000, 0x40a428, 0x12000, 0x15f000, 0xc0000040, L"f8d9aa87d920ec87ca5a7401c44bfd7bcd14b8aa65c881ad4f96c0fdef0b9c1c"}
    }}},
    {L"September 2001 standalone", 0x10c117, 0x4958b, 0x316cf4,
        {0x83, 0xc4, 0x4, 0x8d, 0x85, 0xb8, 0xfd, 0xff, 0xff}, 9, TemporaryRoot::WindowsTemp, {{
        {".text", 0x1000, 0x13ca6a, 0x13d000, 0x1000, 0x60000020, L"4c610c69714d7b630f5583fbcd61f42fd45f5b8a4532cb0447c387152bff0f38"},
        {".rdata", 0x13e000, 0x20ba8, 0x21000, 0x13e000, 0x40000040, L"87ad174fc49ea9a4e6e14f55fbdcfe5c1e17c5f82f003ad98a0c1b9697022805"},
        {".data", 0x15f000, 0x40a428, 0x12000, 0x15f000, 0xc0000040, L"b1a15e6e2ae1f13f45771bc93f8ab04813b5b062da59b29cd63e4500432e06f7"}
    }}}
}};

struct EditorProgram {
    std::uint32_t checkpoint_rva, code_pointer_rva, code_size_rva, arena_pointer_rva;
    std::uint32_t arena_token_limit, inline_token_base, code_token_base;
    std::int32_t word_shift;
    std::uint32_t path_capacity;
    std::vector<std::pair<std::uint32_t,std::uint32_t>> arena_slots;
    std::vector<std::uint32_t> code;

    std::uint32_t slot(std::uint32_t id) const {
        for (const auto& pair : arena_slots) if (pair.first == id) return pair.second;
        return id;
    }
};

struct Target {
    std::filesystem::path executable;
    const RuntimeProfile* profile = nullptr;
    std::shared_ptr<void> file_lock;
    std::shared_ptr<EditorProgram> editor;
    std::filesystem::path folder() const { return executable.parent_path(); }
};
