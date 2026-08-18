#include "CoverArt.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

std::wstring GetDirectory(const std::wstring& path)
{
    const auto pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? std::wstring() : path.substr(0, pos);
}

std::wstring GetExtensionLower(const std::wstring& path)
{
    const auto pos = path.find_last_of(L'.');
    if (pos == std::wstring::npos)
    {
        return {};
    }

    std::wstring ext = path.substr(pos + 1);
    for (auto& ch : ext)
    {
        ch = static_cast<wchar_t>(towlower(ch));
    }

    return ext;
}

bool FileExists(const std::wstring& path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring ExtensionForMime(const std::string& mime)
{
    if (mime.find("png") != std::string::npos)
    {
        return L"png";
    }

    if (mime.find("bmp") != std::string::npos)
    {
        return L"bmp";
    }

    if (mime.find("gif") != std::string::npos)
    {
        return L"gif";
    }

    return L"jpg";
}

std::wstring TempCoverPath(const std::wstring& extension)
{
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    return std::wstring(tempDir) + L"WinampSMTC_cover." + extension;
}

bool WriteImageToTemp(const uint8_t* data, size_t size, const std::wstring& extension, std::wstring& outPath)
{
    if (!data || size == 0)
    {
        return false;
    }

    const std::wstring path = TempCoverPath(extension);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!file)
    {
        return false;
    }

    outPath = path;
    return true;
}

uint32_t ReadSyncSafe32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0] & 0x7F) << 21) |
           (static_cast<uint32_t>(p[1] & 0x7F) << 14) |
           (static_cast<uint32_t>(p[2] & 0x7F) << 7) |
           static_cast<uint32_t>(p[3] & 0x7F);
}

uint32_t ReadBE32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

// Extracts the ID3v2 APIC/PIC picture frame embedded near the start of an MP3 file.
bool ExtractId3v2Picture(std::ifstream& file, std::wstring& outImagePath)
{
    uint8_t header[10]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file || std::memcmp(header, "ID3", 3) != 0)
    {
        return false;
    }

    const uint8_t majorVersion = header[3];
    const uint8_t flags = header[5];
    const uint32_t tagSize = ReadSyncSafe32(&header[6]);

    if (majorVersion < 2 || majorVersion > 4 || tagSize == 0)
    {
        return false;
    }

    std::vector<uint8_t> tag(tagSize);
    file.read(reinterpret_cast<char*>(tag.data()), static_cast<std::streamsize>(tagSize));
    if (!file && !file.eof())
    {
        return false;
    }

    size_t offset = 0;

    // Skip extended header if present.
    if (flags & 0x40)
    {
        if (offset + 4 > tag.size())
        {
            return false;
        }

        const uint32_t extSize = (majorVersion == 4) ? ReadSyncSafe32(&tag[offset]) : ReadBE32(&tag[offset]);
        offset += extSize;
    }

    if (majorVersion == 2)
    {
        // ID3v2.2: 3-char frame id, 3-byte big-endian size.
        while (offset + 6 <= tag.size())
        {
            char id[4]{static_cast<char>(tag[offset]), static_cast<char>(tag[offset + 1]),
                       static_cast<char>(tag[offset + 2]), '\0'};
            if (id[0] == 0)
            {
                break; // Padding reached.
            }

            const uint32_t frameSize = (static_cast<uint32_t>(tag[offset + 3]) << 16) |
                                        (static_cast<uint32_t>(tag[offset + 4]) << 8) |
                                        static_cast<uint32_t>(tag[offset + 5]);
            offset += 6;
            if (frameSize == 0 || offset + frameSize > tag.size())
            {
                break;
            }

            if (std::strcmp(id, "PIC") == 0 && frameSize > 6)
            {
                const uint8_t* frame = &tag[offset];
                const uint8_t encoding = frame[0];
                char format[4] = {static_cast<char>(frame[1]), static_cast<char>(frame[2]), static_cast<char>(frame[3]), '\0'};
                size_t pos = 5; // encoding(1) + format(3) + pictureType(1)
                // Skip description (terminated by 0x00, or 0x00 0x00 if UTF-16).
                const bool wide = (encoding == 1 || encoding == 2);
                while (pos < frameSize)
                {
                    if (wide)
                    {
                        if (pos + 1 < frameSize && frame[pos] == 0 && frame[pos + 1] == 0)
                        {
                            pos += 2;
                            break;
                        }
                        pos += 2;
                    }
                    else
                    {
                        if (frame[pos] == 0)
                        {
                            pos += 1;
                            break;
                        }
                        pos += 1;
                    }
                }

                if (pos < frameSize)
                {
                    std::wstring ext = (std::string(format).find("PNG") != std::string::npos) ? L"png" : L"jpg";
                    return WriteImageToTemp(frame + pos, frameSize - pos, ext, outImagePath);
                }
            }

            offset += frameSize;
        }

        return false;
    }

    // ID3v2.3 / ID3v2.4: 4-char frame id, 4-byte size, 2-byte flags.
    while (offset + 10 <= tag.size())
    {
        char id[5]{static_cast<char>(tag[offset]), static_cast<char>(tag[offset + 1]),
                   static_cast<char>(tag[offset + 2]), static_cast<char>(tag[offset + 3]), '\0'};
        if (id[0] == 0)
        {
            break; // Padding reached.
        }

        const uint32_t frameSize = (majorVersion == 4) ? ReadSyncSafe32(&tag[offset + 4]) : ReadBE32(&tag[offset + 4]);
        offset += 10;
        if (frameSize == 0 || offset + frameSize > tag.size())
        {
            break;
        }

        if (std::strcmp(id, "APIC") == 0 && frameSize > 4)
        {
            const uint8_t* frame = &tag[offset];
            const uint8_t encoding = frame[0];
            size_t pos = 1;

            // MIME type, terminated by 0x00.
            std::string mime;
            while (pos < frameSize && frame[pos] != 0)
            {
                mime.push_back(static_cast<char>(frame[pos]));
                ++pos;
            }
            ++pos; // Skip terminator.
            ++pos; // Skip picture type byte.

            const bool wide = (encoding == 1 || encoding == 2);
            while (pos < frameSize)
            {
                if (wide)
                {
                    if (pos + 1 < frameSize && frame[pos] == 0 && frame[pos + 1] == 0)
                    {
                        pos += 2;
                        break;
                    }
                    pos += 2;
                }
                else
                {
                    if (frame[pos] == 0)
                    {
                        pos += 1;
                        break;
                    }
                    pos += 1;
                }
            }

            if (pos < frameSize)
            {
                return WriteImageToTemp(frame + pos, frameSize - pos, ExtensionForMime(mime), outImagePath);
            }
        }

        offset += frameSize;
    }

    return false;
}

// Extracts the PICTURE metadata block from a FLAC file.
bool ExtractFlacPicture(std::ifstream& file, std::wstring& outImagePath)
{
    char magic[4]{};
    file.read(magic, 4);
    if (!file || std::memcmp(magic, "fLaC", 4) != 0)
    {
        return false;
    }

    for (;;)
    {
        uint8_t blockHeader[4]{};
        file.read(reinterpret_cast<char*>(blockHeader), sizeof(blockHeader));
        if (!file)
        {
            return false;
        }

        const bool isLast = (blockHeader[0] & 0x80) != 0;
        const uint8_t blockType = blockHeader[0] & 0x7F;
        const uint32_t blockSize = (static_cast<uint32_t>(blockHeader[1]) << 16) |
                                    (static_cast<uint32_t>(blockHeader[2]) << 8) |
                                    static_cast<uint32_t>(blockHeader[3]);

        if (blockType != 6)
        {
            file.seekg(blockSize, std::ios::cur);
            if (isLast || !file)
            {
                return false;
            }
            continue;
        }

        std::vector<uint8_t> block(blockSize);
        file.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(blockSize));
        if (!file || block.size() < 8)
        {
            return false;
        }

        size_t pos = 4; // Skip picture type.
        const uint32_t mimeLen = ReadBE32(&block[pos]);
        pos += 4;
        if (pos + mimeLen > block.size())
        {
            return false;
        }

        const std::string mime(reinterpret_cast<char*>(&block[pos]), mimeLen);
        pos += mimeLen;

        if (pos + 4 > block.size())
        {
            return false;
        }

        const uint32_t descLen = ReadBE32(&block[pos]);
        pos += 4 + descLen;

        // Skip width, height, depth, colors used.
        pos += 16;
        if (pos + 4 > block.size())
        {
            return false;
        }

        const uint32_t dataLen = ReadBE32(&block[pos]);
        pos += 4;
        if (pos + dataLen > block.size())
        {
            return false;
        }

        return WriteImageToTemp(&block[pos], dataLen, ExtensionForMime(mime), outImagePath);
    }
}

bool FindFolderImage(const std::wstring& trackPath, std::wstring& outImagePath)
{
    const std::wstring dir = GetDirectory(trackPath);
    if (dir.empty())
    {
        return false;
    }

    for (const wchar_t* name : {L"folder.jpg", L"Folder.jpg", L"folder.png", L"Folder.png"})
    {
        std::wstring candidate = dir + L"\\" + name;
        if (FileExists(candidate))
        {
            outImagePath = candidate;
            return true;
        }
    }

    return false;
}

} // namespace

bool CoverArt::Resolve(const std::wstring& trackPath, std::wstring& outImagePath)
{
    if (trackPath.empty())
    {
        return false;
    }

    if (FindFolderImage(trackPath, outImagePath))
    {
        return true;
    }

    const std::wstring ext = GetExtensionLower(trackPath);
    std::ifstream file(trackPath, std::ios::binary);
    if (file)
    {
        if (ext == L"mp3" && ExtractId3v2Picture(file, outImagePath))
        {
            return true;
        }

        if (ext == L"flac" && ExtractFlacPicture(file, outImagePath))
        {
            return true;
        }
    }

    return false;
}
