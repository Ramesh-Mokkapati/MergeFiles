// Encoding.cpp - text encodings through the Win32 conversion functions.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "Encoding.h"

#include <algorithm>
#include <climits>

namespace mf
{
    namespace
    {
        bool IsValidUtf8(const char* data, int size)
        {
            if (size == 0)
                return true;
            return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, size, nullptr, 0) > 0;
        }

        std::wstring FromCodePage(unsigned codePage, const char* data, int size, bool& lossy)
        {
            if (size <= 0)
                return std::wstring();
            int length = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, data, size, nullptr, 0);
            DWORD flags = MB_ERR_INVALID_CHARS;
            if (length == 0)
            {
                lossy = true;
                flags = 0;
                length = MultiByteToWideChar(codePage, 0, data, size, nullptr, 0);
            }
            std::wstring text(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(codePage, flags, data, size, text.data(), length);
            return text;
        }

        std::wstring FromUtf16(const char* data, size_t size, bool bigEndian, bool& lossy)
        {
            if (size % 2 != 0)
            {
                lossy = true;
                --size;
            }
            std::wstring text(size / 2, L'\0');
            for (size_t i = 0; i < text.size(); ++i)
            {
                const unsigned char b0 = static_cast<unsigned char>(data[2 * i]);
                const unsigned char b1 = static_cast<unsigned char>(data[2 * i + 1]);
                text[i] = static_cast<wchar_t>(bigEndian ? (b0 << 8) | b1 : (b1 << 8) | b0);
            }
            return text;
        }

        // UTF-16 without a BOM: ASCII-range text has a zero byte in every other position.
        bool GuessUtf16(const std::string& bytes, bool& bigEndian)
        {
            const size_t size = (std::min)(bytes.size(), static_cast<size_t>(8192)) & ~static_cast<size_t>(1);
            if (size < 4)
                return false;
            size_t evenZeros = 0, oddZeros = 0;
            for (size_t i = 0; i < size; i += 2)
            {
                evenZeros += bytes[i] == 0;
                oddZeros += bytes[i + 1] == 0;
            }
            const size_t pairs = size / 2;
            if (oddZeros * 10 >= pairs * 4 && evenZeros * 20 < pairs)
            {
                bigEndian = false;
                return true;
            }
            if (evenZeros * 10 >= pairs * 4 && oddZeros * 20 < pairs)
            {
                bigEndian = true;
                return true;
            }
            return false;
        }

        unsigned ResolveCodePage(unsigned codePage)
        {
            return codePage == 0 ? GetACP() : codePage;
        }
    }

    std::wstring EncodingName(const TextEncoding& encoding)
    {
        switch (encoding.type)
        {
        case EncodingType::Utf8:
            return encoding.bom ? L"Unicode (UTF-8 with BOM)" : L"Unicode (UTF-8)";
        case EncodingType::Utf16LE:
            return encoding.bom ? L"Unicode (UTF-16 LE with BOM)" : L"Unicode (UTF-16 LE)";
        case EncodingType::Utf16BE:
            return encoding.bom ? L"Unicode (UTF-16 BE with BOM)" : L"Unicode (UTF-16 BE)";
        default:
            break;
        }
        const unsigned codePage = ResolveCodePage(encoding.codePage);
        CPINFOEXW info = {};
        std::wstring name;
        if (GetCPInfoExW(codePage, 0, &info))
            name = info.CodePageName;  // e.g. "1252  (ANSI - Latin I)"
        else
            name = std::to_wstring(codePage);
        if (codePage == 20932)
            name = L"20932  (EUC-JP)";
        return (encoding.codePage == 0 ? L"ANSI: " : L"Code page ") + name;
    }

    DecodedText DecodeText(const std::string& bytes, const TextEncoding* forced)
    {
        DecodedText result;
        const char* data = bytes.data();
        const size_t size = bytes.size();
        const unsigned char* u = reinterpret_cast<const unsigned char*>(data);
        const bool utf8Bom = size >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF;
        const bool leBom = size >= 2 && u[0] == 0xFF && u[1] == 0xFE;
        const bool beBom = size >= 2 && u[0] == 0xFE && u[1] == 0xFF;

        TextEncoding encoding;
        if (forced)
        {
            encoding = *forced;
            // A forced Unicode encoding still skips (and keeps) a matching BOM.
            if (encoding.type == EncodingType::Utf8)
                encoding.bom = utf8Bom;
            else if (encoding.type == EncodingType::Utf16LE)
                encoding.bom = leBom;
            else if (encoding.type == EncodingType::Utf16BE)
                encoding.bom = beBom;
        }
        else if (utf8Bom)
        {
            encoding = { EncodingType::Utf8, true, 0 };
        }
        else if (leBom)
        {
            encoding = { EncodingType::Utf16LE, true, 0 };
        }
        else if (beBom)
        {
            encoding = { EncodingType::Utf16BE, true, 0 };
        }
        else
        {
            bool bigEndian = false;
            if (size <= INT_MAX && IsValidUtf8(data, static_cast<int>(size)) && bytes.find('\0') == std::string::npos)
                encoding = { EncodingType::Utf8, false, 0 };
            else if (GuessUtf16(bytes, bigEndian))
                encoding = { bigEndian ? EncodingType::Utf16BE : EncodingType::Utf16LE, false, 0 };
            else
                encoding = { EncodingType::CodePage, false, 0 };
        }

        result.encoding = encoding;
        switch (encoding.type)
        {
        case EncodingType::Utf8:
        {
            const size_t skip = encoding.bom ? 3 : 0;
            result.text = FromCodePage(CP_UTF8, data + skip, static_cast<int>(size - skip), result.lossy);
            break;
        }
        case EncodingType::Utf16LE:
        case EncodingType::Utf16BE:
        {
            const size_t skip = encoding.bom ? 2 : 0;
            result.text = FromUtf16(data + skip, size - skip, encoding.type == EncodingType::Utf16BE, result.lossy);
            break;
        }
        default:
            result.text = FromCodePage(ResolveCodePage(encoding.codePage), data, static_cast<int>(size), result.lossy);
            break;
        }
        return result;
    }

    std::string EncodeText(const std::wstring& text, const TextEncoding& encoding, bool* lossy)
    {
        if (lossy)
            *lossy = false;
        std::string bytes;
        const int length = static_cast<int>(text.size());
        switch (encoding.type)
        {
        case EncodingType::Utf8:
        {
            if (encoding.bom)
                bytes = "\xEF\xBB\xBF";
            if (length > 0)
            {
                const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
                const size_t offset = bytes.size();
                bytes.resize(offset + static_cast<size_t>(size));
                WideCharToMultiByte(CP_UTF8, 0, text.data(), length, bytes.data() + offset, size, nullptr, nullptr);
            }
            return bytes;
        }
        case EncodingType::Utf16LE:
        case EncodingType::Utf16BE:
        {
            const bool big = encoding.type == EncodingType::Utf16BE;
            bytes.reserve(text.size() * 2 + 2);
            if (encoding.bom)
                bytes = big ? std::string("\xFE\xFF") : std::string("\xFF\xFE");
            for (wchar_t c : text)
            {
                const char lo = static_cast<char>(c & 0xFF);
                const char hi = static_cast<char>((c >> 8) & 0xFF);
                bytes.push_back(big ? hi : lo);
                bytes.push_back(big ? lo : hi);
            }
            return bytes;
        }
        default:
            break;
        }
        if (length == 0)
            return bytes;
        const unsigned codePage = ResolveCodePage(encoding.codePage);
        BOOL usedDefault = FALSE;
        // Code pages such as UTF-7 or 5022x reject the "used default char" argument.
        BOOL* usedDefaultArg = codePage == 65000 || (codePage >= 50220 && codePage <= 50229) || (codePage >= 57002 && codePage <= 57011) ? nullptr : &usedDefault;
        const int size = WideCharToMultiByte(codePage, 0, text.data(), length, nullptr, 0, nullptr, usedDefaultArg);
        bytes.resize(static_cast<size_t>(size));
        WideCharToMultiByte(codePage, 0, text.data(), length, bytes.data(), size, nullptr, usedDefaultArg);
        if (lossy)
            *lossy = usedDefault != FALSE;
        return bytes;
    }

    bool LooksBinary(const std::string& bytes)
    {
        if (bytes.find('\0') == std::string::npos)
            return false;
        const unsigned char* u = reinterpret_cast<const unsigned char*>(bytes.data());
        if (bytes.size() >= 2 && ((u[0] == 0xFF && u[1] == 0xFE) || (u[0] == 0xFE && u[1] == 0xFF)))
            return false;
        bool bigEndian = false;
        return !GuessUtf16(bytes, bigEndian);
    }

    std::wstring LastErrorText(unsigned long error)
    {
        wchar_t* buffer = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::wstring text = buffer ? buffer : L"";
        if (buffer)
            LocalFree(buffer);
        while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
            text.pop_back();
        if (text.empty())
            text = L"Error " + std::to_wstring(error);
        return text;
    }

    bool ReadFileBytes(const std::wstring& path, std::string& bytes, std::wstring& error)
    {
        bytes.clear();
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = LastErrorText(GetLastError());
            return false;
        }
        LARGE_INTEGER size = {};
        bool ok = GetFileSizeEx(file, &size) != FALSE;
        if (ok && size.QuadPart > 0x7FFFFFFF)
        {
            error = L"The file is too large (over 2 GB).";
            CloseHandle(file);
            return false;
        }
        if (ok)
        {
            bytes.resize(static_cast<size_t>(size.QuadPart));
            DWORD read = 0;
            ok = bytes.empty() || (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size());
        }
        if (!ok)
            error = LastErrorText(GetLastError());
        CloseHandle(file);
        return ok;
    }

    bool WriteFileBytes(const std::wstring& path, const std::string& bytes, std::wstring& error)
    {
        const std::wstring temp = path + L".mergefiles-tmp";
        HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = LastErrorText(GetLastError());
            return false;
        }
        DWORD written = 0;
        const bool ok = bytes.empty() || (WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size());
        const DWORD writeError = GetLastError();
        CloseHandle(file);
        if (!ok)
        {
            DeleteFileW(temp.c_str());
            error = LastErrorText(writeError);
            return false;
        }
        // ReplaceFile keeps the original's attributes and security; it fails when there is no original.
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            if (ReplaceFileW(path.c_str(), temp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr))
                return true;
        }
        if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
            return true;
        error = LastErrorText(GetLastError());
        DeleteFileW(temp.c_str());
        return false;
    }

    bool IsFileReadOnly(const std::wstring& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0;
    }
}
