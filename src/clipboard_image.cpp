#include "clipboard_image.hpp"
#include "logger.hpp"
#include "util.hpp"

#include <cstring>

namespace qp {

namespace {

bool OpenClipboardRetry(HWND owner, int attempts = 30, int sleepMs = 10) {
    for (int i = 0; i < attempts; ++i) {
        if (OpenClipboard(owner)) return true;
        Sleep(static_cast<DWORD>(sleepMs));
    }
    return false;
}

bool CopyHGlobal(HANDLE h, std::vector<unsigned char>& out) {
    out.clear();
    if (!h) return false;
    const SIZE_T n = GlobalSize(h);
    if (n == 0 || n > 64u * 1024u * 1024u) return false;
    void* p = GlobalLock(h);
    if (!p) return false;
    out.resize(static_cast<size_t>(n));
    memcpy(out.data(), p, static_cast<size_t>(n));
    GlobalUnlock(h);
    return true;
}

bool SetFromBytes(UINT format, const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) return false;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!mem) return false;
    void* p = GlobalLock(mem);
    if (!p) {
        GlobalFree(mem);
        return false;
    }
    memcpy(p, bytes.data(), bytes.size());
    GlobalUnlock(mem);
    if (!SetClipboardData(format, mem)) {
        GlobalFree(mem);
        return false;
    }
    return true;
}

} // namespace

bool ClipboardHasImage() {
    if (!OpenClipboardRetry(nullptr)) return false;
    const bool has =
        IsClipboardFormatAvailable(CF_DIB) ||
        IsClipboardFormatAvailable(CF_BITMAP) ||
        IsClipboardFormatAvailable(CF_DIBV5) ||
        IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG"));
    CloseClipboard();
    return has;
}

bool ClipboardSaveImage(ClipboardImage& out, std::wstring* error) {
    out = {};
    if (!OpenClipboardRetry(nullptr)) {
        if (error) *error = L"OpenClipboard failed: " + LastErrorMessage();
        return false;
    }

    out.pngFormat = RegisterClipboardFormatW(L"PNG");

    HANDLE hPng = out.pngFormat ? GetClipboardData(out.pngFormat) : nullptr;
    if (hPng && CopyHGlobal(hPng, out.png)) {
        out.hasPng = true;
        QP_LOG_DEBUG(L"clipboard: saved PNG image (%zu bytes)", out.png.size());
    }

    HANDLE hDib = GetClipboardData(CF_DIB);
    if (hDib && CopyHGlobal(hDib, out.dib)) {
        out.hasDib = true;
        QP_LOG_DEBUG(L"clipboard: saved CF_DIB (%zu bytes)", out.dib.size());
    }

    // CF_BITMAP is a handle — convert via CF_DIB preference; if only BITMAP, try DIBV5
    if (!out.hasDib) {
        HANDLE h5 = GetClipboardData(CF_DIBV5);
        if (h5 && CopyHGlobal(h5, out.dib)) {
            out.hasDib = true;
            QP_LOG_DEBUG(L"clipboard: saved CF_DIBV5 as dib blob (%zu bytes)", out.dib.size());
        }
    }

    CloseClipboard();

    if (out.empty()) {
        if (error) *error = L"clipboard has no supported image format (need PNG or DIB)";
        QP_LOG_WARN(L"clipboard: no image formats found");
        return false;
    }
    return true;
}

bool ClipboardRestoreImage(const ClipboardImage& img, std::wstring* error) {
    if (img.empty()) {
        if (error) *error = L"empty image snapshot";
        return false;
    }
    if (!OpenClipboardRetry(nullptr)) {
        if (error) *error = L"OpenClipboard failed: " + LastErrorMessage();
        return false;
    }
    EmptyClipboard();

    bool ok = false;
    if (img.hasPng && img.pngFormat) {
        ok = SetFromBytes(img.pngFormat, img.png) || ok;
    }
    if (img.hasDib) {
        ok = SetFromBytes(CF_DIB, img.dib) || ok;
    }

    CloseClipboard();
    if (!ok) {
        if (error) *error = L"SetClipboardData image failed";
        QP_LOG_ERROR(L"clipboard: restore image failed");
        return false;
    }
    QP_LOG_DEBUG(L"clipboard: restored image (png=%d dib=%d)",
                 img.hasPng ? 1 : 0, img.hasDib ? 1 : 0);
    return true;
}

} // namespace qp
