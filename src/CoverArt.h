#pragma once

#include <string>

namespace CoverArt {

// Resolves an image file to use as cover art for the given track path.
// Tries folder.jpg/Folder.jpg beside the track first, then falls back to
// embedded artwork (ID3v2 APIC for MP3, PICTURE block for FLAC).
// Returns true and fills outImagePath when an image was found.
bool Resolve(const std::wstring& trackPath, std::wstring& outImagePath);

} // namespace CoverArt
