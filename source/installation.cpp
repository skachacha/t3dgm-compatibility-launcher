#include "shared.hpp"
#include <wincrypt.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;
constexpr char Configuration[] = "FullscreenMode = borderless\r\nDisplayResolution = desktop\r\nResolutionScale = app(1)\r\n";
constexpr std::array<const wchar_t*, 3> Originals{L"DDraw.dll", L"D3DImm.dll", L"setup.ini"};

void plain(const fs::path& path, bool directory = false) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || bool(attributes & FILE_ATTRIBUTE_DIRECTORY) != directory ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Expected a plain " + std::string(directory ? "directory: " : "file: ") + utf8(path.wstring()));
}

std::wstring state(const fs::path& path) {
    if (!fs::exists(path) && !fs::is_symlink(fs::symlink_status(path))) return {};
    plain(path);
    return file_hash(path);
}

Bytes read_bytes(const fs::path& path) {
    plain(path);
    auto length = fs::file_size(path);
    if (length > 1048576) throw std::runtime_error("Configuration file is too large");
    std::ifstream input(path, std::ios::binary);
    Bytes bytes(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!input || input.gcount() != std::streamsize(bytes.size())) throw std::runtime_error("Cannot read configuration file");
    return bytes;
}

std::wstring bytes_hash(const Bytes& bytes) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    win32_need(CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT), "Cannot open hash provider");
    try {
        win32_need(CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash), "Cannot create hash");
        win32_need(CryptHashData(hash, bytes.data(), DWORD(bytes.size()), 0), "Cannot hash payload");
        std::array<BYTE, 32> digest{};
        DWORD length = DWORD(digest.size());
        win32_need(CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &length, 0), "Cannot read payload hash");
        std::wstring result;
        for (auto byte : digest) { result += L"0123456789abcdef"[byte >> 4]; result += L"0123456789abcdef"[byte & 15]; }
        CryptDestroyHash(hash); CryptReleaseContext(provider, 0);
        return result;
    } catch (...) {
        if (hash) CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        throw;
    }
}

Bytes config_bytes() { return Bytes(Configuration, Configuration + sizeof(Configuration) - 1); }

std::wstring lower(std::wstring value) {
    for (auto& character : value) if (character >= L'A' && character <= L'Z') character += L'a' - L'A';
    return value;
}

std::wstring trim(const std::wstring& value) {
    auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    return value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
}

Bytes fullscreen_ini(const Bytes& bytes) {
    enum Encoding { SingleByte, Utf8, Utf16LE, Utf16BE } encoding = SingleByte;
    std::size_t prefix = 0;
    if (bytes.size() >= 3 && bytes[0] == 239 && bytes[1] == 187 && bytes[2] == 191) { encoding = Utf8; prefix = 3; }
    else if (bytes.size() >= 2 && bytes[0] == 255 && bytes[1] == 254) { encoding = Utf16LE; prefix = 2; }
    else if (bytes.size() >= 2 && bytes[0] == 254 && bytes[1] == 255) { encoding = Utf16BE; prefix = 2; }
    std::wstring text;
    if (encoding == Utf8) {
        std::string value(bytes.begin() + prefix, bytes.end());
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
        if (!count && !value.empty()) throw std::runtime_error("Invalid UTF-8 setup.ini");
        text.resize(count);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), text.data(), count);
    } else if (encoding == SingleByte) {
        for (auto byte : bytes) text += wchar_t(byte);
    } else {
        if ((bytes.size() - prefix) % 2) throw std::runtime_error("Invalid UTF-16 setup.ini");
        for (auto at = prefix; at < bytes.size(); at += 2)
            text += wchar_t(encoding == Utf16LE ? bytes[at] | (bytes[at + 1] << 8) : (bytes[at] << 8) | bytes[at + 1]);
    }
    std::wstring section, result;
    unsigned settings = 0, startup = 0;
    for (std::size_t at = 0; at < text.size();) {
        auto end = text.find(L'\n', at);
        end = end == std::wstring::npos ? text.size() : end + 1;
        auto line = text.substr(at, end - at);
        auto stripped = trim(line);
        if (!stripped.empty() && stripped.front() == L'[') {
            auto close = stripped.find(L']');
            if (close != std::wstring::npos) section = lower(trim(stripped.substr(1, close - 1)));
        }
        auto equals = line.find(L'=');
        if (equals != std::wstring::npos) {
            auto key = lower(trim(line.substr(0, equals)));
            if ((section == L"settings" && key == L"windowmode") || (section == L"startup" && key == L"window")) {
                if (section == L"settings") ++settings; else ++startup;
                auto begin = line.find_first_not_of(L" \t", equals + 1);
                if (begin == std::wstring::npos) begin = line.size();
                auto finish = line.find_first_of(L";\r\n", begin);
                if (finish == std::wstring::npos) finish = line.size();
                while (finish > begin && (line[finish - 1] == L' ' || line[finish - 1] == L'\t')) --finish;
                line.replace(begin, finish - begin, L"0");
            }
        }
        result += line;
        at = end;
    }
    if (settings != 1 || startup != 1) throw std::runtime_error("Expected exactly one SETTINGS/windowmode and one STARTUP/window key in setup.ini");
    Bytes output(bytes.begin(), bytes.begin() + prefix);
    if (encoding == Utf8) {
        auto encoded = utf8(result); output.insert(output.end(), encoded.begin(), encoded.end());
    } else for (auto character : result) {
        if (encoding == SingleByte) output.push_back(static_cast<unsigned char>(character));
        else if (encoding == Utf16LE) { output.push_back(character & 255); output.push_back(character >> 8); }
        else { output.push_back(character >> 8); output.push_back(character & 255); }
    }
    return output;
}

void atomic_write(const fs::path& path, const Bytes& bytes) {
    if (fs::exists(path)) plain(path);
    auto temporary = fs::path(path.wstring() + L".t3dgm-writing");
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    win32_need(file != INVALID_HANDLE_VALUE, "Cannot reserve installation temporary file");
    try {
        DWORD count = 0;
        win32_need(WriteFile(file, bytes.data(), DWORD(bytes.size()), &count, nullptr), "Cannot write installation file");
        need(count == bytes.size(), "Cannot write installation file");
        win32_need(FlushFileBuffers(file), "Cannot flush installation file");
        CloseHandle(file); file = INVALID_HANDLE_VALUE;
        win32_need(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "Cannot replace installation file");
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        DeleteFileW(temporary.c_str());
        throw;
    }
}

Bytes wrapper_payload() {
    auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    win32_need(resource != nullptr, "Embedded DDrawCompat resource is missing");
    DWORD size = SizeofResource(nullptr, resource);
    need(size == 3653632, "Embedded DDrawCompat resource size differs");
    auto loaded = LoadResource(nullptr, resource);
    auto pointer = static_cast<const BYTE*>(LockResource(loaded));
    win32_need(pointer != nullptr, "Cannot read embedded DDrawCompat resource");
    Bytes bytes(pointer, pointer + size);
    if (bytes_hash(bytes) != WrapperSha256) throw std::runtime_error("Embedded DDrawCompat SHA256 differs");
    return bytes;
}

bool backup_complete(const fs::path& root) {
    auto old = root / L"old";
    if (!fs::exists(old)) return false;
    plain(old, true);
    if (state(old / L"DDraw.dll") != OriginalDDrawSha256 || state(old / L"D3DImm.dll") != OriginalD3DImmSha256 ||
        !fs::exists(old / L"setup.ini")) return false;
    fullscreen_ini(read_bytes(old / L"setup.ini"));
    return true;
}

void no_running_editor(const fs::path& root) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    win32_need(snapshot != INVALID_HANDLE_VALUE, "Cannot check for a running editor");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof entry;
    bool running = false;
    if (Process32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szExeFile, L"t3dgm.exe")) continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        std::array<wchar_t, 32768> name{}; DWORD count = DWORD(name.size());
        if (!process || !QueryFullProcessImageNameW(process, 0, name.data(), &count)) running = true;
        else running = _wcsicmp(name.data(), (root / L"t3dgm.exe").c_str()) == 0;
        if (process) CloseHandle(process);
        if (running) break;
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    if (running) throw std::runtime_error("Close T3DGM before installing or restoring");
}

void remove_plain(const fs::path& path) {
    if (!fs::exists(path)) return;
    plain(path);
    win32_need(DeleteFileW(path.c_str()), "Cannot remove managed installation file");
}
}

void validate_target(const fs::path& root) {
    verify_file(root / L"t3dgm.exe", TargetSha256);
    verify_file(root / L"d3drm.dll", RetainedModeSha256);
}

Graphics graphics_state(const fs::path& root) {
    auto ddraw = state(root / L"DDraw.dll"), immediate = state(root / L"D3DImm.dll");
    if (ddraw == OriginalDDrawSha256 && immediate == OriginalD3DImmSha256) return Graphics::Original;
    if (ddraw == WrapperSha256 && immediate.empty()) return Graphics::DDrawCompat;
    return Graphics::Unknown;
}

bool restore_available(const fs::path& root) {
    if (!backup_complete(root)) return false;
    for (auto name : Originals) if (state(root / name) != state(root / L"old" / name)) return true;
    return fs::exists(root / L"DDrawCompat-t3dgm.ini");
}

void restore_originals(const fs::path& root) {
    validate_target(root); no_running_editor(root);
    if (!backup_complete(root)) throw std::runtime_error("No complete verified backup was found in old");
    auto old = root / L"old";
    auto original_ini = read_bytes(old / L"setup.ini");
    std::array<std::wstring, 3> original{OriginalDDrawSha256, OriginalD3DImmSha256, bytes_hash(original_ini)};
    std::array<std::wstring, 3> installed{WrapperSha256, L"", bytes_hash(fullscreen_ini(original_ini))};
    for (std::size_t at = 0; at < Originals.size(); ++at) {
        auto current = state(root / Originals[at]);
        if (!current.empty() && current != original[at] && current != installed[at])
            throw std::runtime_error("A managed file changed after installation: " + utf8(Originals[at]));
    }
    auto config = state(root / L"DDrawCompat-t3dgm.ini");
    if (!config.empty() && config != bytes_hash(config_bytes())) throw std::runtime_error("DDrawCompat-t3dgm.ini changed after installation");
    for (std::size_t at = 0; at < Originals.size(); ++at) {
        auto destination = root / Originals[at];
        if (at == 2) atomic_write(destination, original_ini);
        else {
            auto staging = fs::path(destination.wstring() + L".t3dgm-restoring");
            win32_need(CopyFileW((old / Originals[at]).c_str(), staging.c_str(), TRUE), "Cannot stage original DLL");
            try {
                verify_file(staging, original[at].c_str());
                win32_need(MoveFileExW(staging.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "Cannot restore original DLL");
            } catch (...) { DeleteFileW(staging.c_str()); throw; }
        }
        verify_file(destination, original[at].c_str());
    }
    remove_plain(root / L"DDrawCompat-t3dgm.ini");
}

void install_ddrawcompat(const fs::path& root) {
    validate_target(root); no_running_editor(root);
    if (graphics_state(root) != Graphics::Original) throw std::runtime_error("Original Steam DLLs are required before installation, reinstall or verify file integrity in steam");
    for (auto name : {L"DDrawCompat.ini", L"DDrawCompat-t3dgm.ini", L"DDrawCompatOverlay-t3dgm.ini", L"wndmode.dll", L"T3DGM-Compatibility-state.json", L"T3DGM-startup-active.json"})
        if (fs::exists(root / name)) throw std::runtime_error("Keep the existing file and restore/move it aside first: " + utf8(name));
    auto original_ini = read_bytes(root / L"setup.ini");
    auto installed_ini = fullscreen_ini(original_ini);
    auto wrapper = wrapper_payload();
    auto old = root / L"old";
    if (!CreateDirectoryW(old.c_str(), nullptr)) win32_need(GetLastError() == ERROR_ALREADY_EXISTS, "Cannot create old backup folder");
    plain(old, true);
    for (auto name : Originals) {
        auto destination = old / name;
        auto expected = file_hash(root / name);
        if (!fs::exists(destination)) win32_need(CopyFileW((root / name).c_str(), destination.c_str(), TRUE), "Cannot copy original file to old");
        verify_file(destination, expected.c_str());
    }
    try {
        atomic_write(root / L"DDraw.dll", wrapper);
        remove_plain(root / L"D3DImm.dll");
        atomic_write(root / L"setup.ini", installed_ini);
        atomic_write(root / L"DDrawCompat-t3dgm.ini", config_bytes());
        verify_file(root / L"DDraw.dll", WrapperSha256);
    } catch (...) {
        // Keep originals in old even if an interrupted operation needs recovery
        try { restore_originals(root); } catch (...) {}
        throw;
    }
}
