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
public:
    explicit ActionLock(const fs::path& root) {
        std::uint64_t value = 14695981039346656037ull;
        for (wchar_t character : root.wstring()) { value ^= std::towlower(character); value *= 1099511628211ull; }
        auto name = L"Local\\T3DGM-Compatibility-" + std::to_wstring(value);
        mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
        win32_need(mutex_ != nullptr, "Cannot open launcher operation lock");
        DWORD result = WaitForSingleObject(mutex_, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            CloseHandle(mutex_); mutex_ = nullptr;
            throw std::runtime_error("Another launcher action is running for this installation");
        }
    }
    ~ActionLock() { if (mutex_) { ReleaseMutex(mutex_); CloseHandle(mutex_); } }
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
    return L"Unrecognised .dll files";
}

int launch(const fs::path& root, const fs::path& here) {
    validate_target(root);
    auto mode = graphics_state(root);
    if (mode == Graphics::Unknown) throw std::runtime_error("Unrecognised graphics files");
    if (mode == Graphics::DDrawCompat) {
        auto ini = root / L"setup.ini";
        if (GetPrivateProfileIntW(L"SETTINGS", L"windowmode", -1, ini.c_str()) != 0 ||
            GetPrivateProfileIntW(L"STARTUP", L"window", -1, ini.c_str()) != 0)
            throw std::runtime_error("DDrawCompat is installed but fullscreen request settings differ, please restore");
    }
    auto logs = here / L"logs";
    if (!CreateDirectoryW(logs.c_str(), nullptr)) win32_need(GetLastError() == ERROR_ALREADY_EXISTS, "Cannot create logs folder");
    DWORD attributes = GetFileAttributesW(logs.c_str());
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("The logs path is not a plain directory");
    int code = launch_game(root, logs, close_launcher_console);
    if (code) std::wcerr << L"T3DGM exited with an error. See logs\\last-launch.txt.\n";
    return code;
}
}

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    ConsoleAppearance appearance;
    bool check = argc == 2 && std::wstring(argv[1]) == L"--check";
    try {
        if (argc != 1 && !check) throw std::runtime_error("The only command-line option is --check");
        SetConsoleTitleW(L"T3DGM Compatibility");
        auto here = module_folder();
        auto root = game_folder(here);
        validate_target(root);
        if (check) {
            std::wcout << L"Supported executable. Graphics: " << graphics_name(graphics_state(root)) << L".\n";
            return 0;
        }
        std::wstring status;
        bool status_error = false;
        for (;;) {
            auto graphics = graphics_state(root);
            bool restore = graphics == Graphics::DDrawCompat || restore_available(root);
            clear_screen();
            heading(L"T3DGM Compatibility\n\n");
            menu_option(L"1. ", L"Launch The 3D Gamemaker (Steam) with temporary-directory pathname patch [" + std::wstring(graphics_name(graphics)) + L"]");
            menu_option(L"2. ", restore ? L"Restore original .dll files" : L"Install DDrawCompat [recommended]");
            menu_option(L"3. ", L"Exit");
            std::wcout << L"\n" << Recommendation << L"\n";
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
                    return launch(root, here);
                } else if (option == L"2") {
                    confirmation_text(restore ? RestoreText : InstallText);
                    if (confirm()) {
                        ActionLock lock(root);
                        if (restore) restore_originals(root); else install_ddrawcompat(root);
                        status = restore ? L"Original files restored." : L"DDrawCompat installed.";
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
