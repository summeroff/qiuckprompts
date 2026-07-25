#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace qp {

// Snapshot of image-related clipboard formats (does not keep the clipboard open).
struct ClipboardImage {
    bool empty() const { return !hasDib && !hasPng; }

    bool hasDib = false;
    std::vector<unsigned char> dib;   // CF_DIB payload

    bool hasPng = false;
    UINT pngFormat = 0;               // RegisterClipboardFormat(L"PNG")
    std::vector<unsigned char> png;
};

bool ClipboardHasImage();
bool ClipboardSaveImage(ClipboardImage& out, std::wstring* error = nullptr);
bool ClipboardRestoreImage(const ClipboardImage& img, std::wstring* error = nullptr);

} // namespace qp
