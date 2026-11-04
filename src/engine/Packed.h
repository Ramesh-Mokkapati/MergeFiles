// Packed.h - read-only access to .zip and .tar archives (also .tar.gz/.tgz), listed like folders,
// with a built-in DEFLATE decoder (no third-party library). Used for comparing archives as folders
// and for reading Office/OpenDocument files, which are zip archives of XML parts.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mf
{
    // Raw DEFLATE (RFC 1951). `consumed` receives the compressed bytes used. False on corrupt data.
    bool Inflate(const unsigned char* data, size_t size, std::string& out, size_t* consumed = nullptr);
    // gzip (RFC 1952), all members.
    bool Gunzip(const std::string& in, std::string& out);
    uint32_t Crc32(const char* data, size_t size, uint32_t crc = 0);

    struct PackedEntry
    {
        std::wstring path;  // relative, with backslashes, e.g. L"src\\main.cpp"
        bool isDir = false;
        uint64_t size = 0;
        int64_t modified = 0;  // FILETIME ticks (UTC), 0 if unknown

        // Where the data is (internal).
        uint64_t offset = 0;      // zip: local header; tar: data
        uint64_t packedSize = 0;  // zip: compressed size
        int method = 0;           // zip: 0 stored, 8 deflated
        uint32_t crc = 0;
        bool encrypted = false;
        bool implied = false;     // a folder that only appears in other entries' paths
    };

    class PackedArchive
    {
    public:
        // Opens an archive file (zip, tar, tar.gz/tgz; the format is recognised from the content).
        static std::unique_ptr<PackedArchive> Open(const std::wstring& path, std::wstring& error);
        // An archive held in memory (e.g. an Office document read from elsewhere).
        static std::unique_ptr<PackedArchive> FromBytes(std::string bytes, std::wstring& error);

        const std::wstring& Path() const
        {
            return m_path;
        }
        const std::vector<PackedEntry>& Entries() const
        {
            return m_entries;
        }
        // Index of an entry by relative path (case-insensitive, either slash), or -1.
        int Find(const std::wstring& path) const;
        // Entries directly inside folder `folder` ("" = the top level).
        std::vector<int> Children(const std::wstring& folder) const;
        // The contents of a file entry.
        bool Read(int index, std::string& bytes, std::wstring& error) const;
        bool ReadPath(const std::wstring& path, std::string& bytes, std::wstring& error) const;

    private:
        bool ReadAt(uint64_t offset, size_t size, std::string& out) const;
        bool ParseZip(std::wstring& error);
        bool ParseTar(std::wstring& error);
        void AddEntry(PackedEntry entry);
        void Index();

        std::wstring m_path;
        uint64_t m_fileSize = 0;
        bool m_inMemory = false;
        std::string m_data;  // the whole archive when in memory (also a decompressed .tar.gz)
        bool m_zip = false;
        std::vector<PackedEntry> m_entries;
        std::map<std::wstring, int> m_byPath;                   // lower-case path -> entry
        std::map<std::wstring, std::vector<int>> m_children;   // lower-case folder -> entries
    };

    // .zip, .jar, .war, .ear, .nupkg, .vsix, .tar, .tgz, .tar.gz: opened as folders.
    bool IsPackedArchiveName(const std::wstring& path);
}
