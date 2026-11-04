// Packed.cpp - DEFLATE/gzip decoding and the zip and tar readers.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "Packed.h"

#include <algorithm>
#include <cwctype>

#include "Encoding.h"

namespace mf
{
    // ---- DEFLATE (a straightforward canonical-Huffman decoder after RFC 1951) ----

    namespace
    {
        struct Bits
        {
            const unsigned char* in = nullptr;
            size_t size = 0;
            size_t pos = 0;
            uint32_t buffer = 0;
            int count = 0;
            bool overrun = false;

            int Need(int need)
            {
                uint32_t value = buffer;
                while (count < need)
                {
                    if (pos >= size)
                    {
                        overrun = true;
                        return 0;
                    }
                    value |= static_cast<uint32_t>(in[pos++]) << count;
                    count += 8;
                }
                buffer = value >> need;
                count -= need;
                return static_cast<int>(value & ((1u << need) - 1));
            }
        };

        struct Huffman
        {
            short count[16] = {};    // codes of each length
            short symbol[320] = {};  // symbols ordered by code
        };

        int Decode(Bits& bits, const Huffman& h)
        {
            int code = 0, first = 0, index = 0;
            for (int length = 1; length < 16; ++length)
            {
                code |= bits.Need(1);
                if (bits.overrun)
                    return -1;
                const int count = h.count[length];
                if (code - count < first)
                    return h.symbol[index + (code - first)];
                index += count;
                first += count;
                first <<= 1;
                code <<= 1;
            }
            return -1;
        }

        // 0 for a complete code, > 0 incomplete, < 0 over-subscribed.
        int Construct(Huffman& h, const short* length, int n)
        {
            for (short& c : h.count)
                c = 0;
            for (int s = 0; s < n; ++s)
                ++h.count[length[s]];
            if (h.count[0] == n)
                return 0;
            int left = 1;
            for (int len = 1; len < 16; ++len)
            {
                left <<= 1;
                left -= h.count[len];
                if (left < 0)
                    return left;
            }
            short offsets[16] = {};
            for (int len = 1; len < 15; ++len)
                offsets[len + 1] = static_cast<short>(offsets[len] + h.count[len]);
            for (int s = 0; s < n; ++s)
            {
                if (length[s] != 0)
                    h.symbol[offsets[length[s]]++] = static_cast<short>(s);
            }
            return left;
        }

        const short kLengthBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
        const short kLengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
        const unsigned short kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
        const short kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

        bool Codes(Bits& bits, std::string& out, const Huffman& lengths, const Huffman& distances)
        {
            for (;;)
            {
                int symbol = Decode(bits, lengths);
                if (symbol < 0)
                    return false;
                if (symbol < 256)
                {
                    out.push_back(static_cast<char>(symbol));
                    continue;
                }
                if (symbol == 256)
                    return true;
                symbol -= 257;
                if (symbol >= 29)
                    return false;
                const size_t length = static_cast<size_t>(kLengthBase[symbol] + bits.Need(kLengthExtra[symbol]));
                const int code = Decode(bits, distances);
                if (code < 0 || code >= 30)
                    return false;
                const size_t distance = static_cast<size_t>(kDistBase[code] + bits.Need(kDistExtra[code]));
                if (bits.overrun || distance > out.size())
                    return false;
                const size_t from = out.size() - distance;
                for (size_t i = 0; i < length; ++i)
                    out.push_back(out[from + i]);
            }
        }

        bool Stored(Bits& bits, std::string& out)
        {
            bits.buffer = 0;  // the rest of the current byte is padding
            bits.count = 0;
            if (bits.pos + 4 > bits.size)
                return false;
            const unsigned length = bits.in[bits.pos] | (bits.in[bits.pos + 1] << 8);
            const unsigned check = bits.in[bits.pos + 2] | (bits.in[bits.pos + 3] << 8);
            if (length != (~check & 0xFFFFu))
                return false;
            bits.pos += 4;
            if (bits.pos + length > bits.size)
                return false;
            out.append(reinterpret_cast<const char*>(bits.in + bits.pos), length);
            bits.pos += length;
            return true;
        }

        bool Fixed(Bits& bits, std::string& out)
        {
            static Huffman lengths, distances;
            static bool built = false;
            if (!built)
            {
                short length[288];
                int s = 0;
                for (; s < 144; ++s)
                    length[s] = 8;
                for (; s < 256; ++s)
                    length[s] = 9;
                for (; s < 280; ++s)
                    length[s] = 7;
                for (; s < 288; ++s)
                    length[s] = 8;
                Construct(lengths, length, 288);
                for (s = 0; s < 30; ++s)
                    length[s] = 5;
                Construct(distances, length, 30);
                built = true;
            }
            return Codes(bits, out, lengths, distances);
        }

        bool Dynamic(Bits& bits, std::string& out)
        {
            static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            const int nlen = bits.Need(5) + 257;
            const int ndist = bits.Need(5) + 1;
            const int ncode = bits.Need(4) + 4;
            if (bits.overrun || nlen > 286 || ndist > 30)
                return false;
            short length[320] = {};
            for (int i = 0; i < ncode; ++i)
                length[order[i]] = static_cast<short>(bits.Need(3));
            Huffman lengths, distances;
            if (Construct(lengths, length, 19) != 0)
                return false;
            int index = 0;
            while (index < nlen + ndist)
            {
                int symbol = Decode(bits, lengths);
                if (symbol < 0)
                    return false;
                if (symbol < 16)
                {
                    length[index++] = static_cast<short>(symbol);
                    continue;
                }
                short repeated = 0;
                if (symbol == 16)
                {
                    if (index == 0)
                        return false;
                    repeated = length[index - 1];
                    symbol = 3 + bits.Need(2);
                }
                else if (symbol == 17)
                    symbol = 3 + bits.Need(3);
                else
                    symbol = 11 + bits.Need(7);
                if (bits.overrun || index + symbol > nlen + ndist)
                    return false;
                while (symbol--)
                    length[index++] = repeated;
            }
            if (length[256] == 0)
                return false;
            int result = Construct(lengths, length, nlen);
            if (result < 0 || (result > 0 && nlen - lengths.count[0] != 1))
                return false;
            result = Construct(distances, length + nlen, ndist);
            if (result < 0 || (result > 0 && ndist - distances.count[0] != 1))
                return false;
            return Codes(bits, out, lengths, distances);
        }
    }

    bool Inflate(const unsigned char* data, size_t size, std::string& out, size_t* consumed)
    {
        Bits bits;
        bits.in = data;
        bits.size = size;
        int last = 0;
        do
        {
            last = bits.Need(1);
            const int type = bits.Need(2);
            if (bits.overrun)
                return false;
            bool ok = false;
            switch (type)
            {
            case 0:
                ok = Stored(bits, out);
                break;
            case 1:
                ok = Fixed(bits, out);
                break;
            case 2:
                ok = Dynamic(bits, out);
                break;
            default:
                ok = false;
                break;
            }
            if (!ok)
                return false;
        } while (!last);
        if (consumed)
            *consumed = bits.pos;
        return true;
    }

    uint32_t Crc32(const char* data, size_t size, uint32_t crc)
    {
        static uint32_t table[256];
        static bool built = false;
        if (!built)
        {
            for (uint32_t n = 0; n < 256; ++n)
            {
                uint32_t c = n;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                table[n] = c;
            }
            built = true;
        }
        crc ^= 0xFFFFFFFFu;
        for (size_t i = 0; i < size; ++i)
            crc = table[(crc ^ static_cast<unsigned char>(data[i])) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }

    namespace
    {
        uint32_t U16(const std::string& b, size_t at)
        {
            return static_cast<unsigned char>(b[at]) | (static_cast<uint32_t>(static_cast<unsigned char>(b[at + 1])) << 8);
        }
        uint32_t U32(const std::string& b, size_t at)
        {
            return U16(b, at) | (U16(b, at + 2) << 16);
        }
        uint64_t U64(const std::string& b, size_t at)
        {
            return U32(b, at) | (static_cast<uint64_t>(U32(b, at + 4)) << 32);
        }
    }

    bool Gunzip(const std::string& in, std::string& out)
    {
        out.clear();
        size_t pos = 0;
        const auto* bytes = reinterpret_cast<const unsigned char*>(in.data());
        while (pos + 18 <= in.size())
        {
            if (bytes[pos] != 0x1F || bytes[pos + 1] != 0x8B || bytes[pos + 2] != 8)
                return pos > 0;  // trailing bytes after the last member are ignored
            const unsigned flags = bytes[pos + 3];
            size_t p = pos + 10;
            if (flags & 4)
                p += 2 + U16(in, p);
            if (flags & 8)
            {
                while (p < in.size() && bytes[p] != 0)
                    ++p;
                ++p;
            }
            if (flags & 16)
            {
                while (p < in.size() && bytes[p] != 0)
                    ++p;
                ++p;
            }
            if (flags & 2)
                p += 2;
            if (p >= in.size())
                return false;
            std::string member;
            size_t used = 0;
            if (!Inflate(bytes + p, in.size() - p, member, &used))
                return false;
            p += used;
            if (p + 8 > in.size() || U32(in, p) != Crc32(member.data(), member.size()))
                return false;
            out += member;
            pos = p + 8;
        }
        return pos > 0;
    }

    // ---- archives ----

    namespace
    {
        std::wstring Lower(std::wstring text)
        {
            // CharLowerBuff handles every script (towlower in the C locale only knows ASCII).
            if (!text.empty())
                CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
            return text;
        }

        std::wstring Widen(const std::string& bytes, UINT codePage)
        {
            if (bytes.empty())
                return std::wstring();
            DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
            int length = MultiByteToWideChar(codePage, flags, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
            if (length <= 0 && codePage == CP_UTF8)
                return Widen(bytes, CP_ACP);
            std::wstring text(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(codePage, flags, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
            return text;
        }

        // Backslashes, no leading or trailing separators, no "." or ".." parts.
        std::wstring NormalPath(const std::wstring& raw)
        {
            std::wstring result;
            size_t start = 0;
            while (start <= raw.size())
            {
                size_t end = raw.find_first_of(L"\\/", start);
                if (end == std::wstring::npos)
                    end = raw.size();
                const std::wstring part = raw.substr(start, end - start);
                if (!part.empty() && part != L"." && part != L"..")
                    result += (result.empty() ? L"" : L"\\") + part;
                start = end + 1;
            }
            return result;
        }

        int64_t UnixTicks(int64_t seconds)
        {
            return (seconds + 11644473600LL) * 10000000LL;
        }

        int64_t DosTicks(uint32_t date, uint32_t time)
        {
            FILETIME local = {}, utc = {};
            if (!DosDateTimeToFileTime(static_cast<WORD>(date), static_cast<WORD>(time), &local) || !LocalFileTimeToFileTime(&local, &utc))
                return 0;
            return static_cast<int64_t>((static_cast<uint64_t>(utc.dwHighDateTime) << 32) | utc.dwLowDateTime);
        }

        uint64_t Octal(const std::string& block, size_t at, size_t length)
        {
            if (static_cast<unsigned char>(block[at]) & 0x80)
            {
                // GNU base-256 for large values.
                uint64_t value = static_cast<unsigned char>(block[at]) & 0x7F;
                for (size_t i = 1; i < length; ++i)
                    value = (value << 8) | static_cast<unsigned char>(block[at + i]);
                return value;
            }
            uint64_t value = 0;
            size_t i = 0;
            while (i < length && (block[at + i] == ' ' || block[at + i] == '\0'))
                ++i;
            for (; i < length && block[at + i] >= '0' && block[at + i] <= '7'; ++i)
                value = value * 8 + static_cast<uint64_t>(block[at + i] - '0');
            return value;
        }

        std::string CString(const std::string& block, size_t at, size_t length)
        {
            const std::string field = block.substr(at, length);
            return field.substr(0, field.find('\0'));
        }
    }

    std::unique_ptr<PackedArchive> PackedArchive::Open(const std::wstring& path, std::wstring& error)
    {
        std::unique_ptr<PackedArchive> archive(new PackedArchive());
        archive->m_path = path;
        WIN32_FILE_ATTRIBUTE_DATA data = {};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            error = L"Cannot open " + path;
            return nullptr;
        }
        archive->m_fileSize = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        std::string head;
        archive->ReadAt(0, static_cast<size_t>((std::min)(archive->m_fileSize, uint64_t(4))), head);
        if (head.size() >= 2 && static_cast<unsigned char>(head[0]) == 0x1F && static_cast<unsigned char>(head[1]) == 0x8B)
        {
            // .tar.gz: decompressed into memory.
            std::string packed, unpacked;
            if (!ReadFileBytes(path, packed, error))
                return nullptr;
            if (!Gunzip(packed, unpacked))
            {
                error = path + L" is not a valid gzip file.";
                return nullptr;
            }
            archive->m_inMemory = true;
            archive->m_data = std::move(unpacked);
            archive->m_fileSize = archive->m_data.size();
        }
        std::wstring tarError, zipError;
        if (archive->ParseTar(tarError) || archive->ParseZip(zipError))
        {
            archive->Index();
            return archive;
        }
        error = path + L" is not a zip or tar archive" + (zipError.empty() ? L"." : L": " + zipError);
        return nullptr;
    }

    std::unique_ptr<PackedArchive> PackedArchive::FromBytes(std::string bytes, std::wstring& error)
    {
        std::unique_ptr<PackedArchive> archive(new PackedArchive());
        archive->m_inMemory = true;
        archive->m_data = std::move(bytes);
        archive->m_fileSize = archive->m_data.size();
        std::wstring tarError;
        if (archive->ParseZip(error) || archive->ParseTar(tarError))
        {
            archive->Index();
            return archive;
        }
        return nullptr;
    }

    bool PackedArchive::ReadAt(uint64_t offset, size_t size, std::string& out) const
    {
        out.clear();
        if (offset > m_fileSize || size > m_fileSize - offset)
            return false;
        if (m_inMemory)
        {
            out.assign(m_data, static_cast<size_t>(offset), size);
            return true;
        }
        HANDLE file = CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER at;
        at.QuadPart = static_cast<LONGLONG>(offset);
        bool ok = SetFilePointerEx(file, at, nullptr, FILE_BEGIN) != FALSE;
        out.resize(size);
        size_t done = 0;
        while (ok && done < size)
        {
            DWORD read = 0;
            const DWORD want = static_cast<DWORD>((std::min)(size - done, size_t(1) << 24));
            ok = ReadFile(file, out.data() + done, want, &read, nullptr) && read > 0;
            done += read;
        }
        CloseHandle(file);
        if (!ok)
            out.clear();
        return ok;
    }

    bool PackedArchive::ParseZip(std::wstring& error)
    {
        // End of central directory: in the last 64 KB + 22 bytes.
        const uint64_t tail = (std::min)(m_fileSize, uint64_t(65535 + 22));
        std::string end;
        if (tail < 22 || !ReadAt(m_fileSize - tail, static_cast<size_t>(tail), end))
            return false;
        size_t eocd = std::string::npos;
        for (size_t i = end.size() - 22 + 1; i-- > 0;)
        {
            if (U32(end, i) == 0x06054B50)
            {
                eocd = i;
                break;
            }
        }
        if (eocd == std::string::npos)
            return false;
        uint64_t count = U16(end, eocd + 10);
        uint64_t directorySize = U32(end, eocd + 12);
        uint64_t directoryOffset = U32(end, eocd + 16);
        const uint64_t eocdPos = m_fileSize - tail + eocd;
        if ((count == 0xFFFF || directorySize == 0xFFFFFFFF || directoryOffset == 0xFFFFFFFF) && eocd >= 20 && U32(end, eocd - 20) == 0x07064B50)
        {
            std::string record;
            if (ReadAt(U64(end, eocd - 20 + 8), 56, record) && U32(record, 0) == 0x06064B50)
            {
                count = U64(record, 32);
                directorySize = U64(record, 40);
                directoryOffset = U64(record, 48);
            }
        }
        // Data in front of the archive (self-extracting): the directory is then later than recorded.
        const uint64_t shift = eocdPos >= directoryOffset + directorySize ? eocdPos - directoryOffset - directorySize : 0;
        std::string directory;
        if (!ReadAt(directoryOffset + shift, static_cast<size_t>(directorySize), directory))
        {
            error = L"the central directory cannot be read";
            return false;
        }
        m_zip = true;
        size_t p = 0;
        for (uint64_t n = 0; n < count && p + 46 <= directory.size(); ++n)
        {
            if (U32(directory, p) != 0x02014B50)
            {
                error = L"the central directory is damaged";
                return false;
            }
            const uint32_t flags = U16(directory, p + 8);
            PackedEntry entry;
            entry.method = static_cast<int>(U16(directory, p + 10));
            entry.modified = DosTicks(U16(directory, p + 14), U16(directory, p + 12));
            entry.crc = U32(directory, p + 16);
            entry.packedSize = U32(directory, p + 20);
            entry.size = U32(directory, p + 24);
            const size_t nameLength = U16(directory, p + 28), extraLength = U16(directory, p + 30), commentLength = U16(directory, p + 32);
            const uint32_t attributes = U32(directory, p + 38);
            entry.offset = U32(directory, p + 42);
            entry.encrypted = (flags & 1) != 0;
            if (p + 46 + nameLength + extraLength > directory.size())
                break;
            const std::string rawName = directory.substr(p + 46, nameLength);
            // Extra fields: ZIP64 sizes/offset, and the Unix modification time (UTC).
            size_t x = p + 46 + nameLength;
            const size_t extraEnd = x + extraLength;
            while (x + 4 <= extraEnd)
            {
                const uint32_t id = U16(directory, x), length = U16(directory, x + 2);
                size_t v = x + 4;
                if (id == 0x0001)
                {
                    if (entry.size == 0xFFFFFFFF && v + 8 <= x + 4 + length)
                        entry.size = U64(directory, v), v += 8;
                    if (entry.packedSize == 0xFFFFFFFF && v + 8 <= x + 4 + length)
                        entry.packedSize = U64(directory, v), v += 8;
                    if (entry.offset == 0xFFFFFFFF && v + 8 <= x + 4 + length)
                        entry.offset = U64(directory, v);
                }
                else if (id == 0x5455 && length >= 5 && (static_cast<unsigned char>(directory[v]) & 1))
                    entry.modified = UnixTicks(static_cast<int32_t>(U32(directory, v + 1)));
                x += 4 + length;
            }
            entry.offset += shift;
            entry.isDir = (!rawName.empty() && (rawName.back() == '/' || rawName.back() == '\\')) || (attributes & 0x10);
            entry.path = NormalPath(Widen(rawName, (flags & 0x800) ? CP_UTF8 : 437));
            if (!entry.path.empty())
                AddEntry(std::move(entry));
            p += 46 + nameLength + extraLength + commentLength;
        }
        return true;
    }

    bool PackedArchive::ParseTar(std::wstring& error)
    {
        uint64_t pos = 0;
        std::wstring longName;
        std::string paxPath;
        int64_t paxTime = -1;
        uint64_t paxSize = UINT64_MAX;
        bool any = false;
        std::string block;
        while (pos + 512 <= m_fileSize && ReadAt(pos, 512, block))
        {
            if (block.find_first_not_of('\0') == std::string::npos)
                break;  // end of archive
            unsigned sum = 0;
            for (size_t i = 0; i < 512; ++i)
                sum += (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(block[i]);
            if (sum != Octal(block, 148, 8))
            {
                if (!any)
                {
                    error = L"not a tar archive";
                    return false;
                }
                break;
            }
            any = true;
            const char type = block[156];
            uint64_t size = Octal(block, 124, 12);
            if (paxSize != UINT64_MAX && type != 'x' && type != 'g')
                size = paxSize;
            const uint64_t dataPos = pos + 512;
            const uint64_t next = dataPos + (size + 511) / 512 * 512;
            if (type == 'L' || type == 'x' || type == 'g')
            {
                std::string data;
                ReadAt(dataPos, static_cast<size_t>((std::min)(size, uint64_t(1) << 20)), data);
                if (type == 'L')
                    longName = Widen(data.substr(0, data.find('\0')), CP_UTF8);
                else if (type == 'x')
                {
                    // Records "length key=value\n".
                    size_t at = 0;
                    while (at < data.size())
                    {
                        const size_t space = data.find(' ', at);
                        if (space == std::string::npos)
                            break;
                        const size_t length = static_cast<size_t>(strtoull(data.c_str() + at, nullptr, 10));
                        if (length == 0 || at + length > data.size())
                            break;
                        const std::string record = data.substr(space + 1, at + length - space - 2);
                        const size_t equals = record.find('=');
                        if (equals != std::string::npos)
                        {
                            const std::string key = record.substr(0, equals), value = record.substr(equals + 1);
                            if (key == "path")
                                paxPath = value;
                            else if (key == "mtime")
                                paxTime = static_cast<int64_t>(strtod(value.c_str(), nullptr));
                            else if (key == "size")
                                paxSize = strtoull(value.c_str(), nullptr, 10);
                        }
                        at += length;
                    }
                }
                pos = next;
                continue;
            }
            std::string name = CString(block, 0, 100);
            if (block.compare(257, 5, "ustar") == 0)
            {
                const std::string prefix = CString(block, 345, 155);
                if (!prefix.empty())
                    name = prefix + "/" + name;
            }
            PackedEntry entry;
            entry.path = NormalPath(!paxPath.empty() ? Widen(paxPath, CP_UTF8) : (!longName.empty() ? longName : Widen(name, CP_UTF8)));
            entry.isDir = type == '5' || (!name.empty() && name.back() == '/');
            entry.size = entry.isDir ? 0 : size;
            entry.offset = dataPos;
            entry.modified = UnixTicks(paxTime >= 0 ? paxTime : static_cast<int64_t>(Octal(block, 136, 12)));
            const bool regular = type == '0' || type == '\0' || type == '7';
            if (!entry.path.empty() && (regular || entry.isDir))
                AddEntry(std::move(entry));
            longName.clear();
            paxPath.clear();
            paxTime = -1;
            paxSize = UINT64_MAX;
            pos = next;
        }
        if (!any)
            error = L"not a tar archive";
        return any;
    }

    void PackedArchive::AddEntry(PackedEntry entry)
    {
        const std::wstring key = Lower(entry.path);
        auto found = m_byPath.find(key);
        if (found != m_byPath.end())
        {
            m_entries[static_cast<size_t>(found->second)] = std::move(entry);  // a later copy wins
            return;
        }
        m_byPath.emplace(key, static_cast<int>(m_entries.size()));
        m_entries.push_back(std::move(entry));
    }

    void PackedArchive::Index()
    {
        // Folders that only appear in paths.
        const size_t explicitCount = m_entries.size();
        for (size_t i = 0; i < explicitCount; ++i)
        {
            std::wstring path = m_entries[i].path;
            for (size_t slash = path.find_last_of(L'\\'); slash != std::wstring::npos; slash = path.find_last_of(L'\\'))
            {
                path.erase(slash);
                const std::wstring key = Lower(path);
                if (m_byPath.count(key))
                    break;
                PackedEntry folder;
                folder.path = path;
                folder.isDir = true;
                folder.implied = true;
                m_byPath.emplace(key, static_cast<int>(m_entries.size()));
                m_entries.push_back(folder);
            }
        }
        m_children.clear();
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            const std::wstring& path = m_entries[i].path;
            const size_t slash = path.find_last_of(L'\\');
            m_children[slash == std::wstring::npos ? std::wstring() : Lower(path.substr(0, slash))].push_back(static_cast<int>(i));
        }
    }

    int PackedArchive::Find(const std::wstring& path) const
    {
        auto found = m_byPath.find(Lower(NormalPath(path)));
        return found == m_byPath.end() ? -1 : found->second;
    }

    std::vector<int> PackedArchive::Children(const std::wstring& folder) const
    {
        auto found = m_children.find(Lower(NormalPath(folder)));
        return found == m_children.end() ? std::vector<int>() : found->second;
    }

    bool PackedArchive::Read(int index, std::string& bytes, std::wstring& error) const
    {
        bytes.clear();
        if (index < 0 || index >= static_cast<int>(m_entries.size()) || m_entries[static_cast<size_t>(index)].isDir)
        {
            error = L"Not a file in the archive";
            return false;
        }
        const PackedEntry& entry = m_entries[static_cast<size_t>(index)];
        if (entry.size > (uint64_t(1) << 31) || entry.packedSize > (uint64_t(1) << 31))
        {
            error = entry.path + L" is too large to read from the archive";
            return false;
        }
        if (!m_zip)
        {
            if (!ReadAt(entry.offset, static_cast<size_t>(entry.size), bytes))
            {
                error = L"Cannot read " + entry.path + L" from the archive";
                return false;
            }
            return true;
        }
        if (entry.encrypted)
        {
            error = entry.path + L" is encrypted";
            return false;
        }
        std::string header, packed;
        if (!ReadAt(entry.offset, 30, header) || U32(header, 0) != 0x04034B50 ||
            !ReadAt(entry.offset + 30 + U16(header, 26) + U16(header, 28), static_cast<size_t>(entry.packedSize), packed))
        {
            error = L"Cannot read " + entry.path + L" from the archive";
            return false;
        }
        if (entry.method == 0)
            bytes = std::move(packed);
        else if (entry.method == 8)
        {
            bytes.reserve(static_cast<size_t>(entry.size));
            if (!Inflate(reinterpret_cast<const unsigned char*>(packed.data()), packed.size(), bytes))
            {
                error = entry.path + L" is damaged (cannot decompress)";
                return false;
            }
        }
        else
        {
            error = entry.path + L" uses a compression method MergeFiles does not support (" + std::to_wstring(entry.method) + L")";
            return false;
        }
        if (bytes.size() != entry.size || Crc32(bytes.data(), bytes.size()) != entry.crc)
        {
            error = entry.path + L" is damaged (checksum mismatch)";
            return false;
        }
        return true;
    }

    bool PackedArchive::ReadPath(const std::wstring& path, std::string& bytes, std::wstring& error) const
    {
        const int index = Find(path);
        if (index < 0)
        {
            error = path + L" is not in the archive";
            return false;
        }
        return Read(index, bytes, error);
    }

    bool IsPackedArchiveName(const std::wstring& path)
    {
        const std::wstring lower = Lower(path);
        for (const wchar_t* ext : { L".zip", L".jar", L".war", L".ear", L".nupkg", L".vsix", L".tar", L".tgz", L".tar.gz" })
        {
            const size_t length = wcslen(ext);
            if (lower.size() > length && lower.compare(lower.size() - length, length, ext) == 0)
                return true;
        }
        return false;
    }
}
