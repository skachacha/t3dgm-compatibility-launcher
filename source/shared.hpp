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
fs::path game_folder(const fs::path& here);
std::wstring file_hash(const fs::path& path);
void verify_file(const fs::path& path, const wchar_t* expected);
int launch_game(const fs::path& root, const fs::path& log_folder, void (*on_detached)() = nullptr);

enum class Graphics { Original, DDrawCompat, Unknown };
Graphics graphics_state(const fs::path& root);
bool restore_available(const fs::path& root);
void validate_target(const fs::path& root);
void install_ddrawcompat(const fs::path& root);
void restore_originals(const fs::path& root);
