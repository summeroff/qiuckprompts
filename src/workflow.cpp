#include "workflow.hpp"
#include "browser.hpp"
#include "clipboard_image.hpp"
#include "input_sim.hpp"
#include "logger.hpp"
#include "page_ready.hpp"
#include "title_sample.hpp"
#include "util.hpp"

namespace qp {

namespace {

std::wstring PayloadPreview(const std::wstring& s, size_t maxChars = 160) {
    std::wstring out;
    out.reserve(maxChars + 8);
    for (size_t i = 0; i < s.size() && out.size() < maxChars; ++i) {
        const wchar_t c = s[i];
        if (c == L'\r') continue;
        if (c == L'\n') out += L"↵";
        else out.push_back(c);
    }
    if (s.size() > maxChars) out += L"…";
    return out;
}

bool LooksLikeBrowserClass(const std::wstring& cls) {
    return cls == L"Chrome_WidgetWin_1"
        || cls == L"Chrome_WidgetWin_0"
        || cls == L"MozillaWindowClass";
}

} // namespace

AiWorkflow::AiWorkflow(const WorkflowConfig& cfg) : cfg_(cfg) {}

std::wstring AiWorkflow::ComposePayload(const std::wstring& promptBody,
                                        const std::wstring& editorText,
                                        bool fenceEditorText) {
    return BuildPromptPayload(promptBody, editorText, fenceEditorText);
}

bool AiWorkflow::Run(const std::wstring& promptBody,
                     const std::wstring& aiUrlOverride,
                     std::wstring* error) {
    WorkflowRequest req;
    req.promptBody = promptBody;
    req.aiUrl = aiUrlOverride;
    req.captureEditor = true;
    req.fenceEditorText = cfg_.fenceEditorText;
    return Run(req, error);
}

bool AiWorkflow::Run(const WorkflowRequest& req, std::wstring* error) {
    QP_LOG_INFO(L"workflow: BEGIN service=%s label=%s capture=%d image=%d",
                req.service.c_str(), req.label.c_str(),
                req.captureEditor ? 1 : 0, req.requireClipboardImage ? 1 : 0);

    auto fail = [&](const std::wstring& msg) -> bool {
        QP_LOG_ERROR(L"workflow: FAIL %s", msg.c_str());
        if (error) *error = msg;
        return false;
    };

    const std::wstring url = !req.aiUrl.empty() ? req.aiUrl : cfg_.defaultAiUrl;
    if (url.empty()) return fail(L"AI URL is empty");
    if (req.promptBody.empty()) return fail(L"prompt template is empty");

    EnsureComInitialized();

    // Snapshot image early (navigate destroys clipboard).
    ClipboardImage savedImage;
    if (req.requireClipboardImage) {
        if (!ClipboardHasImage()) {
            return fail(L"screenshot binding requires an image on the clipboard "
                        L"(Win+Shift+S, then trigger the hotkey)");
        }
        std::wstring ierr;
        if (!ClipboardSaveImage(savedImage, &ierr)) {
            return fail(ierr.empty() ? L"failed to save clipboard image" : ierr);
        }
        QP_LOG_INFO(L"workflow: clipboard image saved (png=%d dib=%d)",
                    savedImage.hasPng ? 1 : 0, savedImage.hasDib ? 1 : 0);
    }

    ReleaseModifiers(nullptr);
    WaitModifiersReleased(500);
    if (cfg_.afterModifierReleaseMs > 0) {
        Sleep(static_cast<DWORD>(cfg_.afterModifierReleaseMs));
    }

    std::wstring userClipText;
    ClipboardReadUnicode(userClipText, nullptr);

    auto restoreClip = [&]() {
        // Prefer restoring image if we stole one; else prior text.
        if (!savedImage.empty()) {
            ClipboardRestoreImage(savedImage, nullptr);
        } else if (!userClipText.empty()) {
            ClipboardWriteUnicode(userClipText, nullptr);
        }
    };

    std::wstring editorText;
    if (req.captureEditor) {
        const FocusSnapshot source = CaptureFocusSnapshot();
        if (!source.foreground) {
            return fail(L"no foreground window to capture from");
        }
        LogTitleSample(L"workflow_source_editor", source.foreground, source.fgClass);
        if (LooksLikeBrowserClass(source.fgClass)) {
            QP_LOG_WARN(L"workflow: source is a browser — Ctrl+A/C copies the page. "
                        L"Focus your text document first if that was intended.");
        }

        QP_LOG_INFO(L"workflow: select-all + copy");
        if (!SendSelectAll(error)) return fail(error && !error->empty() ? *error : L"Ctrl+A failed");
        if (cfg_.afterSelectAllMs > 0) Sleep(static_cast<DWORD>(cfg_.afterSelectAllMs));
        if (!SendCopy(error)) return fail(error && !error->empty() ? *error : L"Ctrl+C failed");
        if (cfg_.afterCopyMs > 0) Sleep(static_cast<DWORD>(cfg_.afterCopyMs));

        if (!ClipboardReadUnicode(editorText, error)) {
            return fail(error && !error->empty() ? *error : L"clipboard read failed");
        }
        QP_LOG_INFO(L"workflow: captured text (%zu wchar) preview='%s'",
                    editorText.size(), PayloadPreview(editorText, 80).c_str());
    } else {
        QP_LOG_INFO(L"workflow: skip editor capture");
    }

    const bool fence = req.fenceEditorText;
    const std::wstring payload = BuildPromptPayload(req.promptBody, editorText, fence);
    QP_LOG_INFO(L"workflow: payload %zu wchar fence=%d preview='%s'",
                payload.size(), fence ? 1 : 0, PayloadPreview(payload, 200).c_str());

    BrowserTarget browser;
    if (!FindBrowserWindow(cfg_.browserTitleHint, browser, error)) {
        restoreClip();
        return false;
    }
    LogTitleSample(L"workflow_browser_selected", browser.hwnd, browser.title);
    if (!ActivateBrowser(browser, error)) {
        QP_LOG_WARN(L"workflow: ActivateBrowser weak focus — continuing");
    }
    if (cfg_.afterActivateBrowserMs > 0) {
        Sleep(static_cast<DWORD>(cfg_.afterActivateBrowserMs));
    }

    QP_LOG_INFO(L"workflow: new tab");
    ReleaseModifiers(nullptr);
    if (!SendNewTab(error)) {
        restoreClip();
        return fail(error && !error->empty() ? *error : L"Ctrl+T failed");
    }
    if (cfg_.afterNewTabMs > 0) Sleep(static_cast<DWORD>(cfg_.afterNewTabMs));

    QP_LOG_INFO(L"workflow: navigate to %s", url.c_str());
    SendFocusOmnibox(nullptr);
    Sleep(40);
    if (!ClipboardWriteUnicode(url, error)) {
        restoreClip();
        return fail(error && !error->empty() ? *error : L"clipboard url write failed");
    }
    if (!SendPaste(error)) {
        restoreClip();
        return fail(error && !error->empty() ? *error : L"paste URL failed");
    }
    if (cfg_.afterUrlPasteMs > 0) Sleep(static_cast<DWORD>(cfg_.afterUrlPasteMs));
    if (!SendEnter(error)) {
        restoreClip();
        return fail(error && !error->empty() ? *error : L"Enter failed");
    }
    LogTitleSample(L"workflow_after_navigate_enter", browser.hwnd, url);

    {
        BrowserTarget b2;
        if (FindBrowserWindow(cfg_.browserTitleHint, b2, nullptr) && b2.hwnd) {
            browser = b2;
        }
    }

    PageReadyConfig pr;
    pr.browserHwnd = browser.hwnd;
    if (!req.pageTitleHint.empty()) pr.titleHint = req.pageTitleHint;
    else if (!cfg_.pageTitleHint.empty()) pr.titleHint = cfg_.pageTitleHint;
    else pr.titleHint = TitleHintFromUrl(url);
    pr.timeoutMs = cfg_.pageReadyTimeoutMs;
    pr.pollMs = cfg_.pageReadyPollMs;
    pr.minWaitMs = cfg_.pageReadyMinMs;
    pr.settleMs = cfg_.pageReadySettleMs;
    pr.useUia = cfg_.pageReadyUseUia;

    QP_LOG_INFO(L"workflow: page-ready hint='%s'", pr.titleHint.c_str());
    PageReadyResult ready{};
    std::wstring readyErr;
    const bool isReady = WaitForAiPageReady(pr, ready, &readyErr);
    if (!isReady) {
        QP_LOG_WARN(L"workflow: page not ready (%s)", readyErr.c_str());
        if (!cfg_.pasteEvenIfNotReady) {
            restoreClip();
            return fail(readyErr.empty() ? L"page not ready" : readyErr);
        }
    } else {
        QP_LOG_INFO(L"workflow: page ready in %dms (%s) title='%s'",
                    ready.waitedMs, ready.detail.c_str(), ready.title.c_str());
    }

    ActivateBrowser(browser, nullptr);
    Sleep(40);
    ReleaseModifiers(nullptr);
    WaitModifiersReleased(200);

    // --- Paste into AI form ---
    if (req.requireClipboardImage && !savedImage.empty()) {
        QP_LOG_INFO(L"workflow: paste image first");
        std::wstring ierr;
        if (!ClipboardRestoreImage(savedImage, &ierr)) {
            restoreClip();
            return fail(ierr.empty() ? L"restore image failed" : ierr);
        }
        if (!SendPaste(error)) {
            restoreClip();
            return fail(error && !error->empty() ? *error : L"paste image failed");
        }
        if (cfg_.afterImagePasteMs > 0) {
            Sleep(static_cast<DWORD>(cfg_.afterImagePasteMs));
        }
    }

    QP_LOG_INFO(L"workflow: paste text payload (%zu wchar)", payload.size());
    if (!ClipboardWriteUnicode(payload, error)) {
        restoreClip();
        return fail(error && !error->empty() ? *error : L"clipboard payload write failed");
    }
    {
        std::wstring verify;
        if (ClipboardReadUnicode(verify, nullptr) && verify != payload) {
            QP_LOG_WARN(L"workflow: clipboard verify mismatch — rewrite");
            ClipboardWriteUnicode(payload, nullptr);
        }
    }
    if (!SendPaste(error)) {
        QP_LOG_WARN(L"workflow: Ctrl+V text failed, unicode fallback");
        if (!SendUnicodeText(payload, error)) {
            restoreClip();
            return fail(error && !error->empty() ? *error : L"payload paste failed");
        }
    }

    if (cfg_.afterFinalPasteMs > 0) Sleep(static_cast<DWORD>(cfg_.afterFinalPasteMs));
    if (cfg_.clipboardRestoreDelayMs > 0) {
        Sleep(static_cast<DWORD>(cfg_.clipboardRestoreDelayMs));
    }

    restoreClip();
    QP_LOG_INFO(L"workflow: DONE");
    return true;
}

} // namespace qp
