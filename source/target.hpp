#pragma once
#include <array>
#include <cstdint>

// Support the Steam executable
constexpr wchar_t TargetSha256[] = L"e6a1ad971f86a002a42ccacd7d16aa828fbfd7c7c68fb6b7df44e777e1652fe8";
constexpr wchar_t WrapperSha256[] = L"94895cf6ede8fd0f74ee1447fdc08b7044a9c65a1a25a49a4df893a0370975a3";
constexpr wchar_t RetainedModeSha256[] = L"6c1730d441f672d4521cf65bbe9bd19542354c1d3f56d1b76a6345b616c9d281";
constexpr wchar_t OriginalDDrawSha256[] = L"d977bddd66be64b23c60001d88f779e784e0fb48694147103ac57cf008247282";
constexpr wchar_t OriginalD3DImmSha256[] = L"8339438c623662415851062723a4e5141717ff7aa54ab41eb0774c7431db68ad";
constexpr std::uintptr_t PreferredBase = 0x400000;
constexpr std::uintptr_t CheckpointRva = 0x36a6d;
constexpr std::uintptr_t TempBufferRva = 0x2c1b64;
constexpr std::array<unsigned char, 4> CheckpointBytes{0x8d, 0x44, 0x24, 0x18};
