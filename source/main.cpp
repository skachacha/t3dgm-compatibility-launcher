#include "shared.hpp"
#include "launcher_text.hpp"
#include <algorithm>
#include <cwctype>
#include <iostream>
#include <fcntl.h>
#include <io.h>
#include <stdexcept>

namespace {
constexpr WORD LightGreen = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD LightAqua = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
constexpr WORD BrightWhite = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;

void colour(WORD value) {
    std::wcout.flush(); std::wcerr.flush();
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), value);
}

void clear_screen() {
    std::wcout.flush(); std::wcerr.flush();
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(output, &info)) return;
    DWORD cells = DWORD(info.dwSize.X) * DWORD(info.dwSize.Y), count = 0;
    COORD origin{0, 0};
    FillConsoleOutputCharacterW(output, L' ', cells, origin, &count);
    FillConsoleOutputAttribute(output, BrightWhite, cells, origin, &count);
    SetConsoleCursorPosition(output, origin);
}

class ConsoleAppearance {
    WORD original_ = 0;
    bool restore_ = false;
public:
    ConsoleAppearance() {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        restore_ = GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) != FALSE;
        original_ = info.wAttributes;
        colour(BrightWhite);
    }
    ~ConsoleAppearance() { if (restore_) colour(original_); }
};

void heading(const wchar_t* text) {
    colour(LightGreen); std::wcout << text; colour(BrightWhite);
}

void menu_option(const wchar_t* number, const std::wstring& text) {
    colour(LightAqua); std::wcout << number; colour(BrightWhite);
    std::wcout << text << L"\n";
}

void confirmation_text(const wchar_t* text) {
    std::wstring value(text);
    auto newline = value.find(L'\n');
    heading(value.substr(0, newline).c_str());
    if (newline != std::wstring::npos) std::wcout << value.substr(newline);
    std::wcout.flush();
}

void close_launcher_console() noexcept {
    // keep a quiet watcher to make sure temporary files stay valid
    DWORD processes[2]{};
    HWND console = GetConsoleWindow();
    if (console && GetConsoleProcessList(processes, 2) == 1) {
        ShowWindow(console, SW_HIDE);
        FreeConsole();
    }
}

class ActionLock {
    HANDLE mutex_ = nullptr;
    HANDLE directory_ = INVALID_HANDLE_VALUE;
public:
    explicit ActionLock(const fs::path& root) {
        directory_ = CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        win32_need(directory_ != INVALID_HANDLE_VALUE, "Cannot hold selected game folder");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(directory_, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CloseHandle(directory_); directory_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("Selected game folder is not a plain directory");
        }
        auto name = L"Local\\T3DGM-Compatibility-" + std::to_wstring(info.dwVolumeSerialNumber) + L"-" +
            std::to_wstring(info.nFileIndexHigh) + L"-" + std::to_wstring(info.nFileIndexLow);
        mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!mutex_) {
            DWORD error = GetLastError(); CloseHandle(directory_); directory_ = INVALID_HANDLE_VALUE; SetLastError(error);
            win32_need(false, "Cannot open launcher operation lock");
        }
        DWORD result = WaitForSingleObject(mutex_, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            CloseHandle(mutex_); mutex_ = nullptr;
            CloseHandle(directory_); directory_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("Another launcher action is running for this installation");
        }
    }
    ~ActionLock() {
        if (mutex_) { ReleaseMutex(mutex_); CloseHandle(mutex_); }
        if (directory_ != INVALID_HANDLE_VALUE) CloseHandle(directory_);
    }
    ActionLock(const ActionLock&) = delete;
    ActionLock& operator=(const ActionLock&) = delete;
};

bool confirm() {
    std::wstring answer;
    colour(LightAqua);
    bool read = bool(std::getline(std::wcin, answer));
    colour(BrightWhite);
    if (!read) return false;
    answer.erase(std::remove_if(answer.begin(), answer.end(), [](wchar_t c) { return std::iswspace(c); }), answer.end());
    return answer == L"y" || answer == L"Y" || answer == L"yes" || answer == L"YES";
}

const wchar_t* graphics_name(Graphics mode) {
    if (mode == Graphics::DDrawCompat) return L"DDrawCompat";
    if (mode == Graphics::Original) return L"Original .dll files";
    if (mode == Graphics::Devtest) return L"Steam devtest .dll files";
    if (mode == Graphics::System) return L"System graphics (no local wrapper)";
    return L"Other local .dll files";
}

std::wstring target_text(const wchar_t* text, const Target& target) {
    std::wstring result(text);
    for (const auto& pair : {std::pair<std::wstring, std::wstring>{L"%CONFIG%", graphics_config(target).filename().wstring()},
                             {L"%EXE%", target.executable.filename().wstring()}}) {
        std::size_t at = 0;
        while ((at = result.find(pair.first, at)) != std::wstring::npos) {
            result.replace(at, pair.first.size(), pair.second); at += pair.second.size();
        }
    }
    return result;
}

int launch(const Target& target, const fs::path& here) {
    auto logs = here / L"logs";
    if (!CreateDirectoryW(logs.c_str(), nullptr)) win32_need(GetLastError() == ERROR_ALREADY_EXISTS, "Cannot create logs folder");
    DWORD attributes = GetFileAttributesW(logs.c_str());
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("The logs path is not a plain directory");
    auto log = logs / (L"last-launch-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
    int code;
    try { code = launch_game(target, log, close_launcher_console); }
    catch (...) { std::wcerr << L"Launch log: " << log.wstring() << L"\n"; throw; }
    if (code) std::wcerr << L"Application exited with an error. Log: " << log.wstring() << L"\n";
    return code;
}
}

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    ConsoleAppearance appearance;
    bool check = false;
    try {
        fs::path selected;
        for (int at = 1; at < argc; ++at) {
            std::wstring argument(argv[at]);
            if (argument == L"--help") {
                std::wcout << CommandHelp; return 0;
            }
            if (argument == L"--check" && !check) check = true;
            else {
                need(selected.empty() && !argument.empty() && argument.front() != L'-', "Supply one executable filename, optionally with --check");
                selected = argument;
            }
        }
        SetConsoleTitleW(L"T3DGM Compatibility");
        auto here = module_folder();
        auto target = identify_target(selected.empty() ? default_executable(here) : selected);
        auto root = target.folder();
        if (check) {
            std::wcout << L"Supported native runtime: " << target.profile->name << L".\nExecutable: " << target.executable.wstring()
                       << L"\nGraphics: " << graphics_name(graphics_state(root)) << L".\n";
            validate_dependency(target);
            std::wcout << L"Local d3drm.dll: present (32-bit DLL).\n";
            return 0;
        }
        std::wstring status;
        bool status_error = false;
        for (;;) {
            auto graphics = graphics_state(root);
            bool restore = false, add_config = false;
            std::wstring backup_error;
            try { restore = restore_available(root); add_config = restore && !installed_for(target); }
            catch (const std::exception& error) { backup_error = L"Backup check: " + widen(error.what()) + L"\n"; }
            clear_screen();
            heading(L"T3DGM Compatibility 0.3.0\n\n");
            std::wcout << target.executable.filename().wstring() << L"\n" << root.wstring() << L"\n\n";
            menu_option(L"1. ", L"Launch " + target.executable.filename().wstring() + L" with patches [" + std::wstring(graphics_name(graphics)) + L"]");
            menu_option(L"2. ", restore ? L"Restore original .dll files" : L"Install DDrawCompat [recommended]");
            menu_option(L"3. ", L"Exit");
            if (add_config) menu_option(L"4. ", L"Set up DDrawCompat for " + target.executable.filename().wstring());
            std::wcout << L"\n" << Recommendation << L"\n";
            std::wcout << backup_error;
            if (!status.empty()) {
                (status_error ? std::wcerr : std::wcout) << status << L"\n\n";
                status.clear(); status_error = false;
            }
            colour(LightAqua); std::wcout << L"Select an option: " << std::flush;
            std::wstring option;
            bool read = bool(std::getline(std::wcin, option));
            colour(BrightWhite);
            if (!read) break;
            clear_screen();
            if (option == L"3") break;
            try {
                if (option == L"1") {
                    ActionLock lock(root);
                    return launch(target, here);
                } else if (option == L"2" || (option == L"4" && add_config)) {
                    bool do_restore = option == L"2" && restore;
                    confirmation_text(target_text(do_restore ? RestoreText : InstallText, target).c_str());
                    if (confirm()) {
                        ActionLock lock(root);
                        if (do_restore) restore_originals(target); else install_ddrawcompat(target);
                        status = do_restore ? L"Original files restored." : L"DDrawCompat installed.";
                    }
                }
            } catch (const std::exception& error) {
                status = L"Error: " + widen(error.what());
                status_error = true;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::wcerr << L"Error: " << widen(error.what()) << L"\n";
        if (!check) {
            std::wcout << L"Press Enter to exit.\n" << std::flush;
            std::wstring ignored; std::getline(std::wcin, ignored);
        }
        return 1;
    }
}
