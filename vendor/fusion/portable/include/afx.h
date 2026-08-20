#pragma once

// A deliberately small ANSI MFC/Win32 compatibility layer.  It exists only
// to make the FUSION GridMetrics command-line program build on POSIX hosts;
// it is not a general MFC replacement.

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdarg>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <glob.h>
#include <limits.h>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

using BOOL = int;
using BYTE = unsigned char;
using UCHAR = unsigned char;
using WORD = unsigned short;
using DWORD = unsigned long;
using UINT = unsigned int;
using LONG = long;
using TCHAR = char;
using LPCTSTR = const char*;
using LPCSTR = const char*;
using LPTSTR = char*;
using LPSTR = char*;
using HMODULE = void*;
using HKEY = void*;
using std::max;
using std::min;

template <typename Left, typename Right>
constexpr std::common_type_t<Left, Right> min(Left left, Right right) {
    using Result = std::common_type_t<Left, Right>;
    return static_cast<Result>(left) < static_cast<Result>(right)
        ? static_cast<Result>(left) : static_cast<Result>(right);
}

template <typename Left, typename Right>
constexpr std::common_type_t<Left, Right> max(Left left, Right right) {
    using Result = std::common_type_t<Left, Right>;
    return static_cast<Result>(left) > static_cast<Result>(right)
        ? static_cast<Result>(left) : static_cast<Result>(right);
}

constexpr BOOL TRUE = 1;
constexpr BOOL FALSE = 0;
constexpr int _MAX_DRIVE = 3;
constexpr int _MAX_DIR = PATH_MAX;
constexpr int _MAX_PATH = PATH_MAX;
constexpr int _MAX_FNAME = NAME_MAX;
constexpr int _MAX_EXT = NAME_MAX;
constexpr int GetFileExInfoStandard = 0;
constexpr int ERROR_SUCCESS = 0;
constexpr int KEY_ALL_ACCESS = 0;
inline HKEY HKEY_CURRENT_USER = nullptr;

#define _T(value) value
#define _strdup strdup
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _access access
#define _tempnam tempnam
#define _fseeki64 fseeko
#define _ftelli64 ftello
#define __min(left, right) ((left) < (right) ? (left) : (right))
#define __max(left, right) ((left) > (right) ? (left) : (right))
// Match LASzip's platform typedef for FUSION's Windows spelling, including
// pointer parameters.  Linux uses `long` for int64_t; macOS's bundled
// LASzip declarations use `long long`.
#if defined(__APPLE__)
#define __int64 long long
#define _int64 long long
#else
#define __int64 long
#define _int64 long
#endif
#define __int32 int
#define __int16 short
#define __int8 signed char
#define ASSERT(value) ((void)0)
#define DEBUG_NEW new

class CString {
public:
    CString() = default;
    CString(const char* value) : value_(value ? value : "") {}
    CString(const std::string& value) : value_(value) {}

    operator const char*() const { return value_.c_str(); }
    const char* c_str() const { return value_.c_str(); }
    char* GetBufferSetLength(int length) {
        value_.resize(static_cast<size_t>(std::max(0, length)));
        return value_.data();
    }
    char* LockBuffer() { return value_.data(); }
    void UnlockBuffer() {}
    void ReleaseBuffer() { value_.resize(std::strlen(value_.c_str())); }
    void FreeExtra() { value_.shrink_to_fit(); }
    void Empty() { value_.clear(); }
    bool IsEmpty() const { return value_.empty(); }
    int GetLength() const { return static_cast<int>(value_.size()); }
    int Find(char value) const {
        const auto result = value_.find(value);
        return result == std::string::npos ? -1 : static_cast<int>(result);
    }
    int Find(const char* value) const {
        const auto result = value_.find(value ? value : "");
        return result == std::string::npos ? -1 : static_cast<int>(result);
    }
    int Find(const CString& value) const { return Find(value.c_str()); }
    int FindOneOf(const char* values) const {
        const auto result = value_.find_first_of(values ? values : "");
        return result == std::string::npos ? -1 : static_cast<int>(result);
    }
    CString Left(int count) const {
        return CString(value_.substr(0, static_cast<size_t>(std::max(0, count))));
    }
    CString Mid(int first, int count = INT_MAX) const {
        if (first < 0 || static_cast<size_t>(first) >= value_.size()) return {};
        return CString(value_.substr(
            static_cast<size_t>(first), static_cast<size_t>(std::max(0, count))));
    }
    int CompareNoCase(const char* other) const {
        const std::string rhs = other ? other : "";
        auto lhs_it = value_.begin();
        auto rhs_it = rhs.begin();
        for (; lhs_it != value_.end() && rhs_it != rhs.end(); ++lhs_it, ++rhs_it) {
            const int delta = std::tolower(static_cast<unsigned char>(*lhs_it)) -
                              std::tolower(static_cast<unsigned char>(*rhs_it));
            if (delta) return delta;
        }
        return lhs_it == value_.end() && rhs_it == rhs.end()
            ? 0 : (lhs_it == value_.end() ? -1 : 1);
    }
    int CompareNoCase(const CString& other) const { return CompareNoCase(other.c_str()); }
    void MakeLower() {
        std::transform(value_.begin(), value_.end(), value_.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    }
    int Replace(const char* old_value, const char* new_value) {
        const std::string old_text = old_value ? old_value : "";
        if (old_text.empty()) return 0;
        const std::string new_text = new_value ? new_value : "";
        int replacements = 0;
        size_t position = 0;
        while ((position = value_.find(old_text, position)) != std::string::npos) {
            value_.replace(position, old_text.size(), new_text);
            position += new_text.size();
            ++replacements;
        }
        return replacements;
    }
    void TrimLeft(const char* characters = " \t\r\n") {
        const auto first = value_.find_first_not_of(characters ? characters : "");
        value_.erase(0, first == std::string::npos ? value_.size() : first);
    }
    void TrimRight(const char* characters = " \t\r\n") {
        const auto last = value_.find_last_not_of(characters ? characters : "");
        value_.erase(last == std::string::npos ? 0 : last + 1);
    }
    void Insert(int position, const char* text) {
        const auto index = static_cast<size_t>(std::clamp(
            position, 0, static_cast<int>(value_.size())
        ));
        value_.insert(index, text ? text : "");
    }
    void Format(const char* format, ...) {
        // MSVC's printf accepts %I64d/%I64u.  POSIX printf does not; use the
        // ISO C++ spelling for FUSION's explicit 64-bit values.
        std::string portable_format = format ? format : "";
        size_t marker = 0;
        while ((marker = portable_format.find("%I64", marker)) != std::string::npos) {
            portable_format.replace(marker, 4, "%ll");
            marker += 3;
        }
        va_list arguments;
        va_start(arguments, format);
        va_list copy;
        va_copy(copy, arguments);
        const int length = std::vsnprintf(nullptr, 0, portable_format.c_str(), copy);
        va_end(copy);
        value_.resize(static_cast<size_t>(std::max(0, length)));
        std::vsnprintf(
            value_.data(), value_.size() + 1, portable_format.c_str(), arguments
        );
        va_end(arguments);
    }
    CString& operator=(const char* value) {
        value_ = value ? value : "";
        return *this;
    }
    CString& operator+=(const char* value) {
        value_ += value ? value : "";
        return *this;
    }
    CString& operator+=(const CString& value) {
        value_ += value.value_;
        return *this;
    }
    friend CString operator+(const CString& left, const CString& right) {
        return CString(left.value_ + right.value_);
    }
    friend CString operator+(const CString& left, const char* right) {
        return CString(left.value_ + (right ? right : ""));
    }
    friend CString operator+(const char* left, const CString& right) {
        return CString((left ? left : "") + right.value_);
    }

private:
    std::string value_;
};

template <typename TYPE, typename ARG_TYPE = const TYPE&>
class CArray {
public:
    int GetSize() const { return static_cast<int>(values_.size()); }
    void Add(ARG_TYPE value) { values_.push_back(value); }
    void RemoveAt(int index) { values_.erase(values_.begin() + index); }
    void RemoveAll() { values_.clear(); }
    TYPE& operator[](int index) { return values_[index]; }
    const TYPE& operator[](int index) const { return values_[index]; }

private:
    std::vector<TYPE> values_;
};

using CStringArray = CArray<CString, const CString&>;

class CFileFind {
public:
    BOOL FindFile(LPCTSTR specification) {
        Close();
        glob_t result{};
        if (glob(specification, 0, nullptr, &result) != 0) {
            globfree(&result);
            return FALSE;
        }
        for (size_t index = 0; index < result.gl_pathc; ++index) paths_.emplace_back(result.gl_pathv[index]);
        globfree(&result);
        return !paths_.empty();
    }
    BOOL FindNextFile() {
        if (next_ >= paths_.size()) return FALSE;
        current_ = paths_[next_++];
        return next_ < paths_.size();
    }
    BOOL IsDots() const { return FALSE; }
    CString GetFilePath() const { return CString(current_.string()); }
    CString GetFileName() const { return CString(current_.filename().string()); }
    void Close() { paths_.clear(); current_.clear(); next_ = 0; }

private:
    std::vector<std::filesystem::path> paths_;
    std::filesystem::path current_;
    size_t next_ = 0;
};

class CWinApp {};

struct MEMORYSTATUS { size_t dwAvailVirtual = SIZE_MAX; };
inline void GlobalMemoryStatus(MEMORYSTATUS* status) {
    if (status) status->dwAvailVirtual = SIZE_MAX;
}
constexpr int MB_OK = 0;
constexpr int SW_SHOWNORMAL = 0;
inline int MessageBox(void*, LPCTSTR message, LPCTSTR, int) {
    std::fprintf(stderr, "%s\n", message ? message : "");
    return 0;
}
inline void* ShellExecute(void*, LPCTSTR, LPCTSTR, LPCTSTR, LPCTSTR, int) {
    return nullptr;
}

struct FILETIME { DWORD dwLowDateTime = 0; DWORD dwHighDateTime = 0; };
struct SYSTEMTIME {
    WORD wYear = 0, wMonth = 0, wDayOfWeek = 0, wDay = 0;
    WORD wHour = 0, wMinute = 0, wSecond = 0, wMilliseconds = 0;
};
struct WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes = 0;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh = 0, nFileSizeLow = 0;
};

inline std::string& FusionCommandLine() {
    static std::string command_line("GridMetrics");
    return command_line;
}
inline void FusionSetCommandLine(int argc, char* argv[]) {
    auto& command_line = FusionCommandLine();
    command_line.clear();
    for (int index = 0; index < argc; ++index) {
        if (index) command_line += ' ';
        const char* argument = argv[index] ? argv[index] : "";
        if (std::strchr(argument, ' ')) command_line += '"';
        command_line += argument;
        if (std::strchr(argument, ' ')) command_line += '"';
    }
}
inline char* GetCommandLine() {
    return FusionCommandLine().data();
}
inline HMODULE GetModuleHandle(LPCTSTR) { return nullptr; }
inline BOOL AfxWinInit(HMODULE, void*, LPCTSTR, int) { return TRUE; }
inline DWORD GetModuleFileName(HMODULE, char* output, DWORD size) {
    if (!output || !size) return 0;
    const auto path = std::filesystem::current_path() / "GridMetrics";
    std::snprintf(output, size, "%s", path.c_str());
    return static_cast<DWORD>(std::strlen(output));
}
inline DWORD GetTempPath(DWORD size, char* output) {
    std::snprintf(output, size, "%s/", std::filesystem::temp_directory_path().c_str());
    return static_cast<DWORD>(std::strlen(output));
}
inline DWORD GetWindowsDirectory(char* output, DWORD size) { return GetTempPath(size, output); }
inline DWORD GetSystemDirectory(char* output, DWORD size) { return GetTempPath(size, output); }
inline BOOL DeleteFile(LPCTSTR filename) { return std::remove(filename) == 0 ? TRUE : FALSE; }
inline DWORD GetShortPathName(LPCTSTR source, char* output, DWORD size) {
    std::snprintf(output, size, "%s", source ? source : "");
    return static_cast<DWORD>(std::strlen(output));
}
inline int RegOpenKeyEx(HKEY, LPCTSTR, DWORD, DWORD, HKEY* key) {
    if (key) *key = nullptr;
    return 1;
}
inline int RegQueryValueEx(HKEY, LPCTSTR, DWORD*, DWORD*, UCHAR*, DWORD*) { return 1; }
inline int RegCloseKey(HKEY) { return ERROR_SUCCESS; }
inline BOOL GetFileAttributesEx(LPCTSTR filename, int, WIN32_FILE_ATTRIBUTE_DATA* data) {
    struct stat info {};
    if (!filename || !data || stat(filename, &info)) return FALSE;
    const auto size = static_cast<unsigned long long>(info.st_size);
    data->nFileSizeHigh = static_cast<DWORD>(size >> 32);
    data->nFileSizeLow = static_cast<DWORD>(size & 0xffffffffUL);
    const auto nanos = static_cast<unsigned long long>(info.st_mtime) * 1000000000ULL;
    data->ftLastWriteTime.dwHighDateTime = static_cast<DWORD>(nanos >> 32);
    data->ftLastWriteTime.dwLowDateTime = static_cast<DWORD>(nanos & 0xffffffffUL);
    return TRUE;
}
inline BOOL FileTimeToLocalFileTime(const FILETIME* source, FILETIME* target) {
    if (!source || !target) return FALSE;
    *target = *source;
    return TRUE;
}
inline BOOL FileTimeToSystemTime(const FILETIME* filetime, SYSTEMTIME* systemtime) {
    if (!filetime || !systemtime) return FALSE;
    const auto nanos = (static_cast<unsigned long long>(filetime->dwHighDateTime) << 32) |
                      filetime->dwLowDateTime;
    const auto seconds = static_cast<time_t>(nanos / 1000000000ULL);
    const auto milliseconds = static_cast<WORD>((nanos % 1000000000ULL) / 1000000ULL);
    const auto converted = std::localtime(&seconds);
    if (!converted) return FALSE;
    systemtime->wYear = static_cast<WORD>(converted->tm_year + 1900);
    systemtime->wMonth = static_cast<WORD>(converted->tm_mon + 1);
    systemtime->wDay = static_cast<WORD>(converted->tm_mday);
    systemtime->wHour = static_cast<WORD>(converted->tm_hour);
    systemtime->wMinute = static_cast<WORD>(converted->tm_min);
    systemtime->wSecond = static_cast<WORD>(converted->tm_sec);
    systemtime->wMilliseconds = milliseconds;
    return TRUE;
}
