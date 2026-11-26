// Temporary-directory pathname patch and process lifetime
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <wincrypt.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "shared.hpp"
#include "editor_patch.hpp"


class Handle {
    HANDLE value_ = nullptr;
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { if (valid()) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return value_; }
};

void need(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void win32_need(bool condition, const char* message) {
    if (!condition) {
        DWORD code = GetLastError();
        throw std::runtime_error(std::string(message) + " (Win32 " + std::to_string(code) + ")");
    }
}

std::wstring widen(const std::string& text) {
    int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), count);
    return result;
}

std::string utf8(const std::wstring& text) {
    int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}

void write_text(HANDLE file, const std::wstring& text) {
    auto bytes = utf8(text);
    DWORD count = 0;
    win32_need(WriteFile(file, bytes.data(), DWORD(bytes.size()), &count, nullptr), "Cannot write text");
    need(count == bytes.size(), "Cannot write text");
}

std::wstring hash_range(HANDLE file, std::uint64_t offset, std::uint64_t size) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    win32_need(CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT), "Cannot open SHA256 provider");
    try {
        win32_need(CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash), "Cannot create SHA256 hash");
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        win32_need(SetFilePointerEx(file, position, nullptr, FILE_BEGIN), "Cannot seek file");
        std::array<BYTE, 32768> buffer{};
        DWORD count = 0;
        while (size) {
            DWORD requested = DWORD(std::min<std::uint64_t>(size, buffer.size()));
            win32_need(ReadFile(file, buffer.data(), requested, &count, nullptr), "Cannot read file");
            need(count == requested, "File ended inside hash range");
            win32_need(CryptHashData(hash, buffer.data(), count, 0), "Cannot hash file");
            size -= count;
        }
        std::array<BYTE, 32> bytes{};
        count = DWORD(bytes.size());
        win32_need(CryptGetHashParam(hash, HP_HASHVAL, bytes.data(), &count, 0), "Cannot finish SHA256");
        std::wstring result;
        for (BYTE byte : bytes) {
            result += L"0123456789abcdef"[byte >> 4];
            result += L"0123456789abcdef"[byte & 15];
        }
        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        return result;
    } catch (...) {
        if (hash) CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        throw;
    }
}

std::wstring digest(HANDLE file) {
    LARGE_INTEGER size{};
    win32_need(GetFileSizeEx(file, &size), "Cannot measure file");
    need(size.QuadPart >= 0, "Invalid file size");
    return hash_range(file, 0, static_cast<std::uint64_t>(size.QuadPart));
}

Handle* open_file(const fs::path& path) {
    auto file = new Handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file->valid()) { delete file; throw std::runtime_error("Cannot read required file: " + utf8(path.wstring())); }
    BY_HANDLE_FILE_INFORMATION info{};
    bool valid = GetFileInformationByHandle(file->get(), &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
    if (!valid) { delete file; throw std::runtime_error("Required file is not a regular file"); }
    return file;
}

void verify_file(const fs::path& path, const wchar_t* expected) {
    std::unique_ptr<Handle> file(open_file(path));
    need(digest(file->get()) == expected, "Required file has an unsupported SHA256");
}

fs::path module_folder() {
    std::array<wchar_t, 32768> name{};
    DWORD count = GetModuleFileNameW(nullptr, name.data(), DWORD(name.size()));
    win32_need(count != 0, "Cannot locate launcher");
    need(count < name.size(), "Cannot locate launcher");
    return fs::path(std::wstring(name.data(), count)).parent_path();
}

std::string legacy_path(const fs::path& path) {
    auto convert = [](const std::wstring& input) {
        BOOL substituted = FALSE;
        bool unicode = GetACP() == CP_UTF8;
        DWORD flags = unicode ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        int count = WideCharToMultiByte(CP_ACP, flags, input.c_str(), -1, nullptr, 0, nullptr, unicode ? nullptr : &substituted);
        if (!count || substituted) return std::string();
        std::string output(count, '\0');
        if (!WideCharToMultiByte(CP_ACP, flags, input.c_str(), -1, output.data(), count, nullptr,
                                unicode ? nullptr : &substituted) || substituted) return std::string();
        output.pop_back();
        return output;
    };
    auto result = convert(path.wstring());
    if (result.empty() || result.size() > 190) {
        std::array<wchar_t, 32768> short_path{};
        DWORD count = GetShortPathNameW(path.c_str(), short_path.data(), DWORD(short_path.size()));
        if (count && count < short_path.size()) result = convert(std::wstring(short_path.data(), count));
    }
    need(!result.empty() && result.size() <= 190, "Temporary path cannot be represented within DarkBASIC's path limit");
    return result;
}

HANDLE lock_directory(const fs::path& path) {
    HANDLE value = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    win32_need(value != INVALID_HANDLE_VALUE, "Cannot lock temporary directory");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(value, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(value);
        throw std::runtime_error("Temporary directory is not a plain directory");
    }
    return value;
}

class Scratch {
    fs::path parent_, path_;
    std::unique_ptr<Handle> parent_lock_, session_lock_;
    bool cleanup_safe_ = true;
public:
    Scratch() {
        std::array<wchar_t, 32768> temporary{};
        DWORD count = GetTempPathW(DWORD(temporary.size()), temporary.data());
        win32_need(count != 0, "Cannot locate user temporary folder");
        need(count < temporary.size(), "Cannot locate user temporary folder");
        parent_ = fs::absolute(fs::path(std::wstring(temporary.data(), count)) / L"t3dgm").lexically_normal();
        if (!CreateDirectoryW(parent_.c_str(), nullptr)) win32_need(GetLastError() == ERROR_ALREADY_EXISTS, "Cannot create t3dgm temporary folder");
        parent_lock_ = std::make_unique<Handle>(lock_directory(parent_));
        for (DWORD attempt = 0; attempt < 100; ++attempt) {
            wchar_t name[16];
            swprintf(name, 16, L"%08lx", static_cast<unsigned long>(GetCurrentProcessId() + attempt));
            auto candidate = parent_ / name;
            if (CreateDirectoryW(candidate.c_str(), nullptr)) { path_ = candidate; break; }
            win32_need(GetLastError() == ERROR_ALREADY_EXISTS, "Cannot create session temporary folder");
        }
        need(!path_.empty(), "Cannot reserve a fresh session temporary folder");
        session_lock_ = std::make_unique<Handle>(lock_directory(path_));
    }
    ~Scratch() {
        // Leave unexpected directories/files
        if (path_.empty() || !cleanup_safe_) return;
        WIN32_FIND_DATAW item{};
        HANDLE search = FindFirstFileW((path_ / L"*").c_str(), &item);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (!(item.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                    DeleteFileW((path_ / item.cFileName).c_str());
            } while (FindNextFileW(search, &item));
            FindClose(search);
        }
        session_lock_.reset();
        RemoveDirectoryW(path_.c_str());
        // Keep the short parent folder
    }
    const fs::path& path() const { return path_; }
    bool& cleanup_safe() { return cleanup_safe_; }
};

class Log {
    Handle file_;
public:
    explicit Log(const fs::path& path) : file_(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) {
        win32_need(file_.valid(), "Cannot open launcher log");
        BY_HANDLE_FILE_INFORMATION info{};
        win32_need(GetFileInformationByHandle(file_.get(), &info), "Log is not a plain file");
        need(!(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)), "Log is not a plain file");
        win32_need(SetEndOfFile(file_.get()), "Cannot reset launcher log");
    }
    void line(const std::wstring& text) { write_text(file_.get(), text + L"\r\n"); FlushFileBuffers(file_.get()); }
};

void read_remote(HANDLE process, std::uintptr_t address, void* output, SIZE_T size) {
    SIZE_T count = 0;
    win32_need(ReadProcessMemory(process, reinterpret_cast<const void*>(address), output, size, &count), "Cannot read startup state");
    need(count == size, "Cannot read startup state");
}

void write_remote(HANDLE process, std::uintptr_t address, const void* input, SIZE_T size) {
    SIZE_T count = 0;
    win32_need(WriteProcessMemory(process, reinterpret_cast<void*>(address), input, size, &count), "Cannot write startup state");
    need(count == size, "Cannot write startup state");
}

void code_byte(HANDLE process, std::uintptr_t address, BYTE value) {
    DWORD previous = 0, ignored = 0;
    win32_need(VirtualProtectEx(process, reinterpret_cast<void*>(address), 1, PAGE_EXECUTE_READWRITE, &previous), "Cannot prepare startup interception");
    try { write_remote(process, address, &value, 1); }
    catch (...) { VirtualProtectEx(process, reinterpret_cast<void*>(address), 1, previous, &ignored); throw; }
    win32_need(VirtualProtectEx(process, reinterpret_cast<void*>(address), 1, previous, &ignored), "Cannot restore instruction protection");
    win32_need(FlushInstructionCache(process, reinterpret_cast<void*>(address), 1), "Cannot flush startup instruction");
}

void redirect(HANDLE process, const Target& target, const std::string& root) {
    auto address = PreferredBase + target.profile->temporary_rva;
    MEMORY_BASIC_INFORMATION memory{};
    SIZE_T inspected = VirtualQueryEx(process, reinterpret_cast<void*>(address), &memory, sizeof memory);
    win32_need(inspected != 0, "Temporary pathname buffer is not writable");
    need(inspected == sizeof memory && memory.State == MEM_COMMIT && (memory.Protect == PAGE_READWRITE || memory.Protect == PAGE_WRITECOPY) &&
         reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize >= address + 256,
         "Temporary pathname buffer is not writable");
    std::array<char, 256> old{};
    read_remote(process, address, old.data(), old.size());
    need(std::find(old.begin(), old.end(), '\0') != old.end(), "Temporary pathname is not bounded");
    std::string expected;
    if (target.profile->temporary_root == TemporaryRoot::WindowsTemp) {
        std::array<char, 256> windows{};
        UINT count = GetWindowsDirectoryA(windows.data(), UINT(windows.size()));
        win32_need(count != 0, "Cannot identify Windows directory");
        need(count < windows.size(), "Cannot identify Windows directory");
        expected = std::string(windows.data()) + "\\Temp";
    } else {
        auto drive = target.executable.root_name().wstring();
        need(drive.size() == 2 && drive[1] == L':', "Vista runtime requires a drive-letter executable path");
        expected = utf8(drive) + "\\dbTemp";
    }
    need(_stricmp(old.data(), expected.c_str()) == 0, "Original temporary pathname differs from the verified startup state");
    write_remote(process, address, root.c_str(), root.size() + 1);
    std::vector<char> checked(root.size() + 1);
    read_remote(process, address, checked.data(), checked.size());
    need(std::string(checked.data(), checked.size()) == root + '\0', "Temporary pathname readback differs");
}

DWORD run(const Target& target, const std::string& temporary, Log& log, bool& cleanup_safe, void (*on_detached)()) {
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION info{};
    std::wstring command = L"\"" + target.executable.wstring() + L"\"";
    win32_need(CreateProcessW(target.executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                       DEBUG_ONLY_THIS_PROCESS, nullptr, target.folder().c_str(), &startup, &info), "Cannot start selected executable");
    cleanup_safe = false;
    HANDLE own_process = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), info.hProcess, GetCurrentProcess(), &own_process, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        TerminateProcess(info.hProcess, 1);
        if (WaitForSingleObject(info.hProcess, 5000) == WAIT_OBJECT_0) cleanup_safe = true;
        CloseHandle(info.hThread); CloseHandle(info.hProcess);
        throw std::runtime_error("Cannot retain application process handle");
    }
    Handle process(own_process);
    std::vector<HANDLE> startup_handles{info.hProcess, info.hThread};
    auto remember = [&](HANDLE value) {
        if (value && std::find(startup_handles.begin(), startup_handles.end(), value) == startup_handles.end())
            startup_handles.push_back(value);
    };
    auto close_startup_handles = [&](HANDLE thread_in_use = nullptr) {
        for (HANDLE value : startup_handles) {
            DWORD flags = 0;
            if (value != process.get() && value != thread_in_use && GetHandleInformation(value, &flags)) CloseHandle(value);
        }
        startup_handles.clear();
    };
    bool detached = false, event_pending = false, exited = false, loader_break = false, temporary_done = false;
    DEBUG_EVENT event{};
    const auto checkpoint = PreferredBase + target.profile->checkpoint_rva;
    const auto editor_checkpoint = target.editor ? PreferredBase + target.editor->checkpoint_rva : 0;
    const auto started = GetTickCount64();
    try {
        win32_need(DebugSetProcessKillOnExit(TRUE), "Cannot set startup failure handling");
        for (;;) {
            // Covers startup only
            need(GetTickCount64() - started < 30000, "Startup interception timed out");
            if (!WaitForDebugEvent(&event, 100)) {
                win32_need(GetLastError() == ERROR_SEM_TIMEOUT, "Cannot wait for startup event");
                continue;
            }
            event_pending = true;
            DWORD disposition = DBG_CONTINUE;
            if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
                const auto& created = event.u.CreateProcessInfo;
                if (created.hFile) CloseHandle(created.hFile);
                remember(created.hThread);
                remember(created.hProcess);
                if (reinterpret_cast<std::uintptr_t>(created.lpBaseOfImage) != PreferredBase)
                    throw std::runtime_error("Unexpected executable image base: " + std::to_string(reinterpret_cast<std::uintptr_t>(created.lpBaseOfImage)));
                std::vector<BYTE> bytes(target.profile->signature_size);
                read_remote(process.get(), checkpoint, bytes.data(), bytes.size());
                need(std::equal(bytes.begin(), bytes.end(), target.profile->signature.begin()), "Startup instruction signature differs");
                code_byte(process.get(), checkpoint, 0xcc);
                if (editor_checkpoint) {
                    std::array<BYTE,3> instruction{};
                    read_remote(process.get(),editor_checkpoint,instruction.data(),instruction.size());
                    need(instruction==std::array<BYTE,3>{{0x83,0xc4,0x04}},"Editor startup checkpoint differs");
                    code_byte(process.get(),editor_checkpoint,0xcc);
                }
            } else if (event.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT) {
                remember(event.u.CreateThread.hThread);
            } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
                if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
            } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
                exited = true;
                throw std::runtime_error("Application exited before temporary-path correction");
            } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
                const auto& exception = event.u.Exception;
                auto address = reinterpret_cast<std::uintptr_t>(exception.ExceptionRecord.ExceptionAddress);
                if (exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT &&
                    (address == checkpoint || (editor_checkpoint && address == editor_checkpoint))) {
                    Handle thread(OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                                             FALSE, event.dwThreadId));
                    win32_need(thread.valid(), "Cannot open startup thread");
                    CONTEXT context{};
                    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                    win32_need(GetThreadContext(thread.get(), &context), "Unexpected startup instruction pointer");
                    need(context.Eip == address + 1, "Unexpected startup instruction pointer");
                    if (address == checkpoint) {
                        need(!temporary_done,"Temporary checkpoint repeated");
                        redirect(process.get(), target, temporary);
                        code_byte(process.get(), checkpoint, target.profile->signature[0]);
                        temporary_done = true;
                        log.line(L"Temporary pathname corrected.");
                    } else {
                        need(temporary_done && (context.Eax & 0xff) == 1,"Editor program loader did not complete successfully");
                        log.line(apply_editor_patch(process.get(),*target.editor));
                        code_byte(process.get(), editor_checkpoint, 0x83);
                    }
                    context.Eip = DWORD(address);
                    win32_need(SetThreadContext(thread.get(), &context), "Cannot restore startup instruction pointer");
                    if (address == checkpoint && target.editor) {
                        win32_need(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,DBG_CONTINUE),"Cannot continue to editor program checkpoint");
                        event_pending = false;
                        continue;
                    }
                    win32_need(SuspendThread(thread.get()) != DWORD(-1), "Cannot hold startup thread for detachment");
                    win32_need(ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE), "Cannot continue startup event");
                    event_pending = false;
                    win32_need(DebugSetProcessKillOnExit(FALSE), "Cannot release debugger exit ownership");
                    win32_need(DebugActiveProcessStop(info.dwProcessId), "Cannot detach startup debugger");
                    detached = true;
                    close_startup_handles(thread.get());
                    if (ResumeThread(thread.get()) == DWORD(-1)) {
                        // A failed resume must not leave an unusable owned child.
                        TerminateProcess(process.get(), 1);
                        throw std::runtime_error("Cannot resume the corrected application");
                    }
                    try { log.line(L"Temporary pathname corrected; original instruction restored; debugger detached."); } catch (...) {}
                    if (on_detached) on_detached();
                    break;
                } else if (exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT && !loader_break) {
                    loader_break = true; // Windows' initial loader breakpoint.
                } else {
                    disposition = DBG_EXCEPTION_NOT_HANDLED;
                }
            }
            win32_need(ContinueDebugEvent(event.dwProcessId, event.dwThreadId, disposition), "Cannot continue startup event");
            event_pending = false;
        }
    } catch (...) {
        if (!detached && !exited) TerminateProcess(process.get(), 1);
        if (event_pending) ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE);
        if (!detached) {
            // Drain only our terminated child. Do not leave an owned process
            // paused on an exception when the launcher exits after failure.
            auto until = GetTickCount64() + 3000;
            while (!exited && GetTickCount64() < until) {
                DEBUG_EVENT ending{};
                if (WaitForDebugEvent(&ending, 100)) {
                    if (ending.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && ending.u.LoadDll.hFile) CloseHandle(ending.u.LoadDll.hFile);
                    exited = ending.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT;
                    ContinueDebugEvent(ending.dwProcessId, ending.dwThreadId, DBG_CONTINUE);
                }
            }
        }
        if (WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0) cleanup_safe = true;
        close_startup_handles();
        throw;
    }
    DWORD wait = WaitForSingleObject(process.get(), INFINITE);
    if (wait == WAIT_FAILED) win32_need(false, "Cannot wait for application shutdown");
    need(wait == WAIT_OBJECT_0, "Cannot wait for application shutdown");
    cleanup_safe = true;
    DWORD code = 0;
    win32_need(GetExitCodeProcess(process.get(), &code), "Cannot read application exit code");
    log.line(L"Application exited: " + std::to_wstring(code));
    return code;
}

std::wstring file_hash(const fs::path& path) {
    std::unique_ptr<Handle> file(open_file(path));
    return digest(file->get());
}

int launch_game(const Target& target, const fs::path& log_file, void (*on_detached)()) {
    validate_dependency(target);
    Log log(log_file);
    log.line(L"T3DGM Compatibility 0.4.0");
    log.line(L"Executable: " + target.executable.wstring());
    log.line(L"Native runtime: " + std::wstring(target.profile->name));
    log.line(target.editor ? L"Identified editor program: multilevel save correction enabled." :
        L"Editor save correction not applied: standalone or unverified editor program.");
    Scratch scratch;
    auto temporary = legacy_path(scratch.path());
    log.line(L"Temporary files on disk: " + scratch.path().wstring());
    DWORD code = run(target, temporary, log, scratch.cleanup_safe(), on_detached);
    return code == 0 ? 0 : 1;
}
