#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <filesystem>
#include <string>
#include "target.hpp"

namespace fs = std::filesystem;
void need(bool condition, const char* message);
void win32_need(bool condition, const char* message);
std::string utf8(const std::wstring& text);
std::wstring widen(const std::string& text);
fs::path module_folder();
fs::path default_executable(const fs::path& here);
Target identify_target(const fs::path& executable);
void validate_dependency(const Target& target);
std::wstring hash_range(HANDLE file, std::uint64_t offset, std::uint64_t size);
std::wstring file_hash(const fs::path& path);
void verify_file(const fs::path& path, const wchar_t* expected);
int launch_game(const Target& target, const fs::path& log_file, void (*on_detached)() = nullptr);

enum class Graphics { System, Original, Devtest, DDrawCompat, Other };
Graphics graphics_state(const fs::path& root);
fs::path graphics_config(const Target& target);
bool restore_available(const fs::path& root);
bool installed_for(const Target& target);
void install_ddrawcompat(const Target& target);
void restore_originals(const Target& target);
