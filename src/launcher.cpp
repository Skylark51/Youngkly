#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <zlib.h>
#include "StartupSplash.h"

namespace fs = std::filesystem;
static std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), s.size(), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8 payload path");
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), s.size(), out.data(), n);
    return out;
}
static std::wstring quote(const std::wstring &s) {
    std::wstring out = L"\"";
    int slash = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            ++slash;
            continue;
        }
        if (c == L'\"') {
            out.append(slash * 2 + 1, L'\\');
            out += c;
        } else {
            out.append(slash, L'\\');
            out += c;
        }
        slash = 0;
    }
    out.append(slash * 2, L'\\');
    return out + L"\"";
}
static void extract(const unsigned char *data, size_t size, const fs::path &root) {
    auto u16 = [&](size_t p) -> uint16_t {
        if (p + 2 > size)
            throw std::runtime_error("Truncated payload");
        return data[p] | (data[p + 1] << 8);
    };
    auto u32 = [&](size_t p) -> uint32_t {
        if (p + 4 > size)
            throw std::runtime_error("Truncated payload");
        return uint32_t(data[p]) | (uint32_t(data[p + 1]) << 8) | (uint32_t(data[p + 2]) << 16) |
               (uint32_t(data[p + 3]) << 24);
    };
    if (size < 22)
        throw std::runtime_error("Payload missing");
    size_t end = size - 22;
    while (u32(end) != 0x06054b50) {
        if (end == 0 || size - end > 65557)
            throw std::runtime_error("Payload ZIP directory missing");
        --end;
    }
    size_t pos = u32(end + 16);
    for (unsigned i = 0; i < u16(end + 10); ++i) {
        if (u32(pos) != 0x02014b50)
            throw std::runtime_error("Invalid payload directory");
        const auto method = u16(pos + 10), nl = u16(pos + 28);
        const uint32_t packed = u32(pos + 20), unpacked = u32(pos + 24), offset = u32(pos + 42),
                       crc = u32(pos + 16);
        if (pos + 46 + nl > size)
            throw std::runtime_error("Invalid payload name");
        std::string name(reinterpret_cast<const char *>(data + pos + 46), nl);
        pos += 46 + nl + u16(pos + 30) + u16(pos + 32);
        if (name.empty())
            continue;
        auto relative = fs::path(wide(name));
        if (relative.is_absolute() || relative.has_root_name())
            throw std::runtime_error("Absolute payload path");
        for (const auto &part : relative)
            if (part == L"..")
                throw std::runtime_error("Unsafe payload path");
        auto target = root / relative;
        if (name.back() == '/') {
            fs::create_directories(target);
            continue;
        }
        if (u32(offset) != 0x04034b50)
            throw std::runtime_error("Invalid payload record");
        const size_t start = offset + 30 + u16(offset + 26) + u16(offset + 28);
        if (start + packed > size)
            throw std::runtime_error("Truncated payload data");
        std::vector<unsigned char> output(unpacked ? unpacked : 1);
        if (method == 0) {
            if (packed != unpacked)
                throw std::runtime_error("Invalid stored payload");
            std::copy(data + start, data + start + packed, output.begin());
        } else if (method == 8) {
            z_stream stream{};
            stream.next_in = const_cast<Bytef *>(data + start);
            stream.avail_in = packed;
            stream.next_out = output.data();
            stream.avail_out = output.size();
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
                throw std::runtime_error("Payload inflate init failed");
            int result = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
            if (result != Z_STREAM_END || stream.total_out != unpacked)
                throw std::runtime_error("Payload inflate failed");
        } else
            throw std::runtime_error("Payload compression unsupported");
        if (::crc32(0, output.data(), unpacked) != crc)
            throw std::runtime_error("Payload CRC failed");
        fs::create_directories(target.parent_path());
        std::ofstream file(target, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<char *>(output.data()), unpacked);
        if (!file)
            throw std::runtime_error("Cannot extract runtime file");
    }
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    try {
        StartupSplash splash;
        wchar_t own[32768];
        DWORD n = GetModuleFileNameW(nullptr, own, 32768);
        if (!n)
            throw std::runtime_error("Executable path unavailable");
        fs::path workspace = fs::path(std::wstring(own, n)).parent_path();
        auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
        if (!resource)
            throw std::runtime_error("Embedded runtime missing");
        auto size = SizeofResource(nullptr, resource);
        auto data = static_cast<const unsigned char *>(LockResource(LoadResource(nullptr, resource)));
        if (!data)
            throw std::runtime_error("Embedded runtime unreadable");
        wchar_t local[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local)))
            throw std::runtime_error("Local application data unavailable");
        std::wstringstream tag;
        tag << std::hex << ::crc32(0, data, size) << L"-" << size;
        auto runtime = fs::path(local) / L"Youngkly" / (L"runtime-" + tag.str());
        auto mutex = CreateMutexW(nullptr, FALSE, (L"Local\\Youngkly-" + tag.str()).c_str());
        if (!mutex)
            throw std::runtime_error("Runtime lock failed");
        DWORD wait = WaitForSingleObject(mutex, 120000);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
            throw std::runtime_error("Another Youngkly launch is preparing the runtime");
        if (!fs::exists(runtime / L"ready") || !fs::exists(runtime / L"youngkly_app.exe")) {
            fs::create_directories(runtime);
            extract(data, size, runtime);
            std::ofstream(runtime / L"ready") << "complete";
        }
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        auto executable = runtime / L"youngkly_app.exe";
        std::wstring command = quote(executable.wstring()) + L" --workspace " + quote(workspace.wstring());
        int count = 0;
        auto args = CommandLineToArgvW(GetCommandLineW(), &count);
        for (int i = 1; i < count; ++i)
            command += L" " + quote(args[i]);
        LocalFree(args);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        // Finish the three-second splash before the application can show its window.
        splash.finish();
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            workspace.c_str(), &startup, &process))
            throw std::runtime_error("Cannot start Youngkly application (Windows error " +
                                     std::to_string(GetLastError()) + ")");
        CloseHandle(process.hThread);
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hProcess);
        return code;
    } catch (const std::exception &e) {
        MessageBoxW(nullptr, wide(e.what()).c_str(), L"Youngkly", MB_OK | MB_ICONERROR);
        return 1;
    }
}
