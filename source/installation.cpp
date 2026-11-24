#include "shared.hpp"
#include <wincrypt.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;
constexpr char Configuration[] = "FullscreenMode = borderless\r\nDisplayResolution = desktop\r\nResolutionScale = app(1)\r\n";
constexpr wchar_t LedgerName[] = L"compatibility-state.txt";
constexpr std::array<const wchar_t*, 3> SharedNames{L"DDraw.dll", L"D3DImm.dll", L"setup.ini"};

void plain(const fs::path& path, bool directory = false) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    need(attributes != INVALID_FILE_ATTRIBUTES && bool(attributes & FILE_ATTRIBUTE_DIRECTORY) == directory &&
         !(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "Expected a plain installation file or directory");
}
std::wstring state(const fs::path& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        win32_need(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND, "Cannot inspect installation file");
        return {};
    }
    plain(path); return file_hash(path);
}
Bytes read_bytes(const fs::path& path, std::uintmax_t limit = 64 * 1024 * 1024) {
    plain(path);
    auto length = fs::file_size(path);
    need(length <= limit, "Managed file exceeds the installation size limit");
    std::ifstream input(path, std::ios::binary);
    Bytes bytes(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    need(bool(input) && input.gcount() == std::streamsize(bytes.size()), "Cannot read installation file");
    return bytes;
}
std::wstring bytes_hash(const Bytes& bytes) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    win32_need(CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT), "Cannot open hash provider");
    try {
        win32_need(CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash), "Cannot create hash");
        win32_need(CryptHashData(hash, bytes.data(), DWORD(bytes.size()), 0), "Cannot hash payload");
        std::array<BYTE, 32> digest{}; DWORD length = DWORD(digest.size());
        win32_need(CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &length, 0), "Cannot read payload hash");
        std::wstring result;
        for (auto byte : digest) { result += L"0123456789abcdef"[byte >> 4]; result += L"0123456789abcdef"[byte & 15]; }
        CryptDestroyHash(hash); CryptReleaseContext(provider, 0); return result;
    } catch (...) {
        if (hash) CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0); throw;
    }
}
Bytes config_bytes() { return Bytes(Configuration, Configuration + sizeof(Configuration) - 1); }
std::wstring lower(std::wstring value) {
    for (auto& c : value) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return value;
}
std::wstring trim(const std::wstring& value) {
    auto first = value.find_first_not_of(L" \t\r\n");
    return first == std::wstring::npos ? L"" : value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
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
        need(count || value.empty(), "Invalid UTF-8 setup.ini");
        text.resize(count);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), text.data(), count);
    } else if (encoding == SingleByte) {
        for (auto byte : bytes) text += wchar_t(byte);
    } else {
        need((bytes.size() - prefix) % 2 == 0, "Invalid UTF-16 setup.ini");
        for (auto at = prefix; at < bytes.size(); at += 2)
            text += wchar_t(encoding == Utf16LE ? bytes[at] | (bytes[at + 1] << 8) : (bytes[at] << 8) | bytes[at + 1]);
    }
    need(text.find(L'\0') == std::wstring::npos, "NUL character in setup.ini");
    const std::wstring newline = text.find(L"\r\n") != std::wstring::npos ? L"\r\n" :
        text.find(L'\n') != std::wstring::npos ? L"\n" : L"\r\n";
    std::vector<std::wstring> lines;
    for (std::size_t at = 0; at < text.size();) {
        auto end = text.find(L'\n', at); end = end == std::wstring::npos ? text.size() : end + 1;
        lines.push_back(text.substr(at, end - at)); at = end;
    }
    // Check precedence before changing any lines.
    std::array<int, 2> sections{-1, -1}, keys{-1, -1};
    std::wstring section;
    for (std::size_t at = 0; at < lines.size(); ++at) {
        auto stripped = trim(lines[at]);
        if (!stripped.empty() && stripped.front() == L'[') {
            auto close = stripped.find(L']');
            if (close != std::wstring::npos) {
                section = lower(trim(stripped.substr(1, close - 1)));
                int slot = section == L"settings" ? 0 : section == L"startup" ? 1 : -1;
                if (slot >= 0) { need(sections[slot] == -1, "Duplicate fullscreen section in setup.ini"); sections[slot] = int(at); }
            }
        }
        auto equals = lines[at].find(L'=');
        if (equals == std::wstring::npos) continue;
        auto key = lower(trim(lines[at].substr(0, equals)));
        int slot = section == L"settings" && key == L"windowmode" ? 0 : section == L"startup" && key == L"window" ? 1 : -1;
        if (slot >= 0) { need(keys[slot] == -1, "Duplicate fullscreen key in setup.ini"); keys[slot] = int(at); }
    }
    for (int slot = 0; slot != 2; ++slot) if (keys[slot] >= 0) {
        auto& line = lines[keys[slot]]; auto begin = line.find(L'=') + 1;
        while (begin < line.size() && (line[begin] == L' ' || line[begin] == L'\t')) ++begin;
        auto end = line.find_first_of(L";\r\n", begin);
        if (end == std::wstring::npos) end = line.size();
        while (end > begin && (line[end - 1] == L' ' || line[end - 1] == L'\t')) --end;
        line.replace(begin, end - begin, L"0");
    }
    std::wstring result;
    auto append = [&](const std::wstring& line) {
        if (!result.empty() && result.back() != L'\n') result += newline;
        result += line;
    };
    for (std::size_t at = 0; at < lines.size(); ++at) {
        result += lines[at];
        for (int slot = 0; slot != 2; ++slot)
            if (sections[slot] == int(at) && keys[slot] == -1)
                append((slot == 0 ? L"windowmode=0" : L"window=0") + newline);
    }
    for (int slot = 0; slot != 2; ++slot) if (sections[slot] == -1) {
        append((slot == 0 ? L"[SETTINGS]" : L"[STARTUP]") + newline);
        append((slot == 0 ? L"windowmode=0" : L"window=0") + newline);
    }
    Bytes output(bytes.begin(), bytes.begin() + prefix);
    if (encoding == Utf8) {
        auto encoded = utf8(result); output.insert(output.end(), encoded.begin(), encoded.end());
    } else for (auto c : result) {
        if (encoding == SingleByte) output.push_back(static_cast<unsigned char>(c));
        else if (encoding == Utf16LE) { output.push_back(c & 255); output.push_back(c >> 8); }
        else { output.push_back(c >> 8); output.push_back(c & 255); }
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
        DeleteFileW(temporary.c_str()); throw;
    }
}
void remove_plain(const fs::path& path) {
    if (state(path).empty()) return;
    win32_need(DeleteFileW(path.c_str()), "Cannot remove managed installation file");
}
Bytes wrapper_payload() {
    auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    win32_need(resource != nullptr, "Embedded DDrawCompat resource is missing");
    DWORD size = SizeofResource(nullptr, resource);
    need(size == 3653632, "Embedded DDrawCompat resource size differs");
    auto loaded = LoadResource(nullptr, resource);
    auto pointer = static_cast<const BYTE*>(LockResource(loaded));
    win32_need(pointer != nullptr, "Cannot read embedded DDrawCompat");
    Bytes bytes(pointer, pointer + size);
    need(bytes_hash(bytes) == WrapperSha256, "Embedded DDrawCompat SHA256 differs");
    return bytes;
}

struct Record { std::wstring name, original, installed; };
using Records = std::vector<Record>;
bool valid_hash(const std::wstring& value) {
    return value.empty() || (value.size() == 64 && value.find_first_not_of(L"0123456789abcdef") == std::wstring::npos);
}
bool config_name(const std::wstring& name) {
    return name.size() > 16 && lower(name).substr(0, 12) == L"ddrawcompat-" && lower(name).substr(name.size() - 4) == L".ini";
}
void validate_records(const Records& records) {
    need(records.size() >= 4 && records.size() <= 515, "Invalid backup ledger size");
    for (std::size_t at = 0; at < records.size(); ++at) {
        const auto& r = records[at];
        need(!r.name.empty() && r.name.find_first_of(L"\\/:*?\"<>|\t\r\n") == std::wstring::npos &&
             r.name.back() != L'.' && r.name.back() != L' ' &&
             (at < 3 ? r.name == SharedNames[at] : config_name(r.name)), "Invalid backup ledger filename");
        need(valid_hash(r.original) && valid_hash(r.installed), "Invalid backup ledger hash");
        for (std::size_t before = 0; before < at; ++before)
            need(_wcsicmp(records[before].name.c_str(), r.name.c_str()) != 0, "Duplicate backup ledger filename");
    }
    need(records[0].installed == WrapperSha256 && records[1].installed.empty() && !records[2].installed.empty(),
         "Unexpected installation hashes in backup ledger");
    for (std::size_t at = 3; at < records.size(); ++at)
        need(records[at].installed == bytes_hash(config_bytes()), "Unexpected configuration hash in backup ledger");
}
Bytes ledger_bytes(const Records& records) {
    validate_records(records);
    std::wstring text = L"T3DGM Compatibility backup 1\r\n";
    for (const auto& r : records)
        text += L"F\t" + r.name + L"\t" + (r.original.empty() ? L"-" : r.original) + L"\t" +
                (r.installed.empty() ? L"-" : r.installed) + L"\r\n";
    auto bytes = utf8(text); return Bytes(bytes.begin(), bytes.end());
}
Records load_records(const fs::path& root) {
    auto old = root / L"old";
    if (!fs::exists(old)) return {};
    plain(old, true);
    Records records;
    if (fs::exists(old / LedgerName)) {
        auto bytes = read_bytes(old / LedgerName, 1024 * 1024);
        std::string encoded(bytes.begin(), bytes.end());
        auto text = widen(encoded);
        need(utf8(text) == encoded, "Invalid UTF-8 backup ledger");
        std::wistringstream input(text); std::wstring line;
        need(bool(std::getline(input, line)) && line == L"T3DGM Compatibility backup 1\r", "Unsupported backup ledger");
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            auto a = line.find(L'\t'), b = line.find(L'\t', a + 1), c = line.find(L'\t', b + 1);
            need(a == 1 && line[0] == L'F' && b != std::wstring::npos && c != std::wstring::npos &&
                 line.find(L'\t', c + 1) == std::wstring::npos, "Invalid backup ledger row");
            auto original = line.substr(b + 1, c - b - 1), installed = line.substr(c + 1);
            records.push_back({line.substr(a + 1, b - a - 1), original == L"-" ? L"" : original, installed == L"-" ? L"" : installed});
        }
    } else {
        if (fs::is_empty(old)) return {};
        // Version 0.1.0 kept exactly these three original Steam files.
        for (const auto& item : fs::directory_iterator(old)) {
            bool known = false;
            for (auto name : SharedNames) known |= _wcsicmp(item.path().filename().c_str(), name) == 0;
            need(known, "The old folder contains an unrecognised backup; keep it and move it aside first");
        }
        need(state(old / SharedNames[0]) == OriginalDDrawSha256 &&
             state(old / SharedNames[1]) == OriginalD3DImmSha256 && !state(old / SharedNames[2]).empty(),
             "No complete verified 0.1.0 backup in old");
        auto ini = fullscreen_ini(read_bytes(old / SharedNames[2], 1024 * 1024));
        records = {{SharedNames[0], OriginalDDrawSha256, WrapperSha256},
                   {SharedNames[1], OriginalD3DImmSha256, L""},
                   {SharedNames[2], state(old / SharedNames[2]), bytes_hash(ini)},
                   {L"DDrawCompat-t3dgm.ini", L"", bytes_hash(config_bytes())}};
    }
    validate_records(records);
    for (const auto& r : records)
        need(state(old / r.name) == r.original, "An original backup is missing or changed; nothing has been restored");
    return records;
}
void verify_current(const fs::path& root, const Records& records) {
    for (const auto& r : records) {
        auto current = state(root / r.name);
        need(current.empty() || current == r.original || current == r.installed,
             "A managed file changed after installation; keep it and restore it manually before continuing");
    }
}
void no_running_application(const fs::path& root) {
    std::vector<std::wstring> names;
    for (const auto& item : fs::directory_iterator(root))
        if (_wcsicmp(item.path().extension().c_str(), L".exe") == 0) names.push_back(item.path().filename().wstring());
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    win32_need(snapshot != INVALID_HANDLE_VALUE, "Cannot check for a running application");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof entry;
    bool running = false;
    if (Process32FirstW(snapshot, &entry)) do {
        if (entry.th32ProcessID == GetCurrentProcessId()) continue;
        if (std::none_of(names.begin(), names.end(), [&](const auto& n) { return _wcsicmp(n.c_str(), entry.szExeFile) == 0; })) continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        std::array<wchar_t, 32768> name{}; DWORD count = DWORD(name.size());
        if (!process || !QueryFullProcessImageNameW(process, 0, name.data(), &count)) running = true;
        else running = _wcsicmp(fs::path(name.data()).parent_path().c_str(), root.c_str()) == 0;
        if (process) CloseHandle(process);
        if (running) break;
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    need(!running, "Close applications in the selected game folder before installing or restoring");
}
using Snapshot = std::vector<std::pair<fs::path, std::optional<Bytes>>>;
Snapshot snapshot_files(const fs::path& root, const Records& records) {
    Snapshot result;
    for (const auto& r : records) {
        auto path = root / r.name;
        result.push_back({path, state(path).empty() ? std::optional<Bytes>{} : read_bytes(path)});
    }
    return result;
}
void put(const fs::path& path, const std::optional<Bytes>& bytes) {
    if (bytes) {
        if (state(path) != bytes_hash(*bytes)) atomic_write(path, *bytes);
    } else remove_plain(path);
}
void rollback(const Snapshot& snapshot) {
    for (const auto& [path, bytes] : snapshot) put(path, bytes);
}
void save_baseline(const fs::path& root, const Records& records) {
    auto old = root / L"old";
    if (!CreateDirectoryW(old.c_str(), nullptr))
        win32_need(GetLastError() == ERROR_ALREADY_EXISTS, "Cannot create old backup folder");
    plain(old, true);
    for (const auto& r : records) {
        if (r.original.empty()) { need(state(old / r.name).empty(), "Unexpected original backup file"); continue; }
        if (state(old / r.name).empty()) {
            auto bytes = read_bytes(root / r.name);
            need(bytes_hash(bytes) == r.original, "Original file changed while preparing backup");
            atomic_write(old / r.name, bytes);
        }
        need(state(old / r.name) == r.original, "Original backup differs");
    }
    // Record absence and installed hashes before replacing anything.
    atomic_write(old / LedgerName, ledger_bytes(records));
}
}

fs::path graphics_config(const Target& target) {
    return target.folder() / (L"DDrawCompat-" + target.executable.stem().wstring() + L".ini");
}
Graphics graphics_state(const fs::path& root) {
    auto ddraw = state(root / L"DDraw.dll"), immediate = state(root / L"D3DImm.dll");
    if (ddraw.empty() && immediate.empty()) return Graphics::System;
    if (ddraw == OriginalDDrawSha256 && immediate == OriginalD3DImmSha256) return Graphics::Original;
    if (ddraw == DevtestDDrawSha256 && immediate == DevtestD3DImmSha256) return Graphics::Devtest;
    if (ddraw == WrapperSha256 && immediate.empty()) return Graphics::DDrawCompat;
    return Graphics::Other;
}
bool restore_available(const fs::path& root) {
    auto records = load_records(root);
    return std::any_of(records.begin(), records.end(), [&](const auto& r) { return state(root / r.name) != r.original; });
}
bool installed_for(const Target& target) {
    auto records = load_records(target.folder());
    auto name = graphics_config(target).filename().wstring();
    return !records.empty() && graphics_state(target.folder()) == Graphics::DDrawCompat &&
        state(target.folder() / SharedNames[2]) == records[2].installed &&
        std::any_of(records.begin() + 3, records.end(), [&](const auto& r) {
            return _wcsicmp(r.name.c_str(), name.c_str()) == 0 && state(target.folder() / r.name) == r.installed;
        });
}
void install_ddrawcompat(const Target& target) {
    validate_dependency(target);
    const auto root = target.folder();
    no_running_application(root);
    auto records = load_records(root);
    if (!records.empty()) verify_current(root, records);
    if (records.empty()) {
        for (auto name : SharedNames) records.push_back({name, state(root / name), L""});
        records[0].installed = WrapperSha256;
        records[2].installed = bytes_hash(fullscreen_ini(records[2].original.empty() ? Bytes{} : read_bytes(root / SharedNames[2], 1024 * 1024)));
    }
    auto name = graphics_config(target).filename().wstring();
    if (std::none_of(records.begin(), records.end(), [&](const auto& r) { return _wcsicmp(r.name.c_str(), name.c_str()) == 0; }))
        records.push_back({name, state(root / name), bytes_hash(config_bytes())});
    validate_records(records);
    auto wrapper = wrapper_payload();
    Bytes ini = fullscreen_ini(records[2].original.empty() ? Bytes{} :
        fs::exists(root / L"old" / SharedNames[2]) ? read_bytes(root / L"old" / SharedNames[2], 1024 * 1024) : read_bytes(root / SharedNames[2], 1024 * 1024));
    need(bytes_hash(ini) == records[2].installed, "Fullscreen configuration does not match the backup ledger");
    auto snapshot = snapshot_files(root, records);
    save_baseline(root, records);
    try {
        put(root / SharedNames[0], wrapper);
        put(root / SharedNames[1], std::nullopt);
        put(root / SharedNames[2], ini);
        put(root / name, config_bytes());
    } catch (...) {
        try { rollback(snapshot); }
        catch (...) { throw std::runtime_error("Installation failed and rollback was incomplete; keep old for restoration"); }
        throw;
    }
}
void restore_originals(const Target& target) {
    const auto root = target.folder();
    no_running_application(root);
    auto records = load_records(root);
    need(!records.empty(), "No verified original backup was found in old");
    verify_current(root, records);
    auto snapshot = snapshot_files(root, records);
    try {
        for (const auto& r : records)
            put(root / r.name, r.original.empty() ? std::optional<Bytes>{} : read_bytes(root / L"old" / r.name));
    } catch (...) {
        try { rollback(snapshot); }
        catch (...) { throw std::runtime_error("Restoration failed and rollback was incomplete; originals remain in old"); }
        throw;
    }
}
