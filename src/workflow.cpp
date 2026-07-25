#include "workflow.hpp"
#include "browser.hpp"
#include "input_sim.hpp"
#include "logger.hpp"
#include "page_ready.hpp"
#include "title_sample.hpp"
#include "util.hpp"

namespace qp {

namespace {

std::wstring TrimRightCopy(std::wstring s) {
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' ||
                          s.back() == L'\r' || s.back() == L'\n')) {
        s.pop_back();
    }
    return s;
}

// One-line preview for logs (newlines → ↵, truncated).
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
    std::wstring prompt = TrimRightCopy(promptBody);
    if (editorText.empty()) {
        return prompt;
    }

    std::wstring out;
    if (fenceEditorText) {
        // Clear structure so the model (and the user glancing at the box)
        // can tell instructions from the source text.
        //
        //   <prompt>:
        //
        //   ```
        //   <editor text>
        //   ```
        //
        out.reserve(prompt.size() + editorText.size() + 32);
        out += prompt;
        if (!out.empty() && out.back() == L':') {
            // already has colon
        } else {
            out.push_back(L':');
        }
        out += L"\n\n```\n";
        out += editorText;
        if (!editorText.empty() && editorText.back() != L'\n') {
            out.push_back(L'\n');
        }
        out += L"```\n";
    } else {
        out = prompt;
        if (!out.empty() && out.back() != L'\n') out += L"\n\n";
        else if (!out.empty()) out += L"\n";
        out += editorText;
    }
    return out;
}

bool AiWorkflow::Run(const std::wstring& promptBody,
                     const std::wstring& aiUrlOverride,
                     std::wstring* error) {
    QP_LOG_INFO(L"workflow: BEGIN SendToAi");

    auto fail = [&](const std::wstring& msg) -> bool {
        QP_LOG_ERROR(L"workflow: FAIL %s", msg.c_str());
        if (error) *error = msg;
        return false;
    };

    const std::wstring url = !aiUrlOverride.empty() ? aiUrlOverride : cfg_.defaultAiUrl;
    if (url.empty()) return fail(L"AI URL is empty");
    if (promptBody.empty()) return fail(L"prompt template is empty");

    EnsureComInitialized();

    ReleaseModifiers(nullptr);
    WaitModifiersReleased(500);
    if (cfg_.afterModifierReleaseMs > 0) {
        Sleep(static_cast<DWORD>(cfg_.afterModifierReleaseMs));
    }

    const FocusSnapshot source = CaptureFocusSnapshot();
    if (!source.foreground) {
        return fail(L"no foreground window to capture from");
    }
    LogTitleSample(L"workflow_source_editor", source.foreground, source.fgClass);
    if (LooksLikeBrowserClass(source.fgClass)) {
        QP_LOG_WARN(L"workflow: source looks like a browser (class='%s' title='%s') — "
                    L"Ctrl+A/C will copy the web page, not a desktop editor. "
                    L"Focus your text document first if that was intended.",
                    source.fgClass.c_str(), source.fgTitle.c_str());
    }

    std::wstring userClip;
    ClipboardReadUnicode(userClip, nullptr);

    auto restoreClip = [&]() {
        if (!userClip.empty()) {
            ClipboardWriteUnicode(userClip, nullptr);
        }
    };

    QP_LOG_INFO(L"workflow: select-all + copy from focused window");
    if (!SendSelectAll(error)) return fail(error && !error->empty() ? *error : L"Ctrl+A failed");
    if (cfg_.afterSelectAllMs > 0) Sleep(static_cast<DWORD>(cfg_.afterSelectAllMs));

    if (!SendCopy(error)) return fail(error && !error->empty() ? *error : L"Ctrl+C failed");
    if (cfg_.afterCopyMs > 0) Sleep(static_cast<DWORD>(cfg_.afterCopyMs));

    std::wstring editorText;
    if (!ClipboardReadUnicode(editorText, error)) {
        return fail(error && !error->empty() ? *error : L"clipboard read failed");
    }
    QP_LOG_INFO(L"workflow: captured text (%zu wchar) preview='%s'",
                editorText.size(), PayloadPreview(editorText, 80).c_str());
    if (editorText.empty()) {
        QP_LOG_WARN(L"workflow: captured text empty — continuing with prompt only");
    }

    const std::wstring payload =
        ComposePayload(promptBody, editorText, cfg_.fenceEditorText);

    QP_LOG_INFO(L"workflow: payload %zu wchar (prompt %zu + text %zu fence=%d)",
                payload.size(), promptBody.size(), editorText.size(),
                cfg_.fenceEditorText ? 1 : 0);
    QP_LOG_INFO(L"workflow: payload preview='%s'",
                PayloadPreview(payload, 200).c_str());

    // Sanity: payload must start with prompt content (first non-space of template).
    {
        const std::wstring ptrim = TrimRightCopy(promptBody);
        if (!ptrim.empty() && payload.find(ptrim.substr(0, (std::min)(ptrim.size(), size_t{24}))) == std::wstring::npos) {
            QP_LOG_ERROR(L"workflow: payload does not contain prompt prefix — abort paste");
            restoreClip();
            return fail(L"internal error: payload missing prompt");
        }
    }

    BrowserTarget browser;
    if (!FindBrowserWindow(cfg_.browserTitleHint, browser, error)) {
        restoreClip();
        return false;
    }
    LogTitleSample(L"workflow_browser_selected", browser.hwnd, browser.title);
    if (!ActivateBrowser(browser, error)) {
        QP_LOG_WARN(L"workflow: ActivateBrowser weak focus — continuing anyway");
    }
    LogForegroundTitle(L"workflow_after_activate");
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
    LogTitleSample(L"workflow_after_newtab", browser.hwnd);

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
    LogTitleSample(L"workflow_before_page_ready", browser.hwnd);

    PageReadyConfig pr;
    pr.browserHwnd = browser.hwnd;
    pr.titleHint = !cfg_.pageTitleHint.empty() ? cfg_.pageTitleHint
                                               : TitleHintFromUrl(url);
    pr.timeoutMs = cfg_.pageReadyTimeoutMs;
    pr.pollMs    = cfg_.pageReadyPollMs;
    pr.minWaitMs = cfg_.pageReadyMinMs;
    pr.settleMs  = cfg_.pageReadySettleMs;
    pr.useUia    = cfg_.pageReadyUseUia;
    pr.preferFocusedEdit = true;
    pr.focusFoundEdit = true;

    QP_LOG_INFO(L"workflow: smart page-ready (titleHint='%s' timeout=%d uia=%d)",
                pr.titleHint.c_str(), pr.timeoutMs, pr.useUia ? 1 : 0);

    PageReadyResult ready{};
    std::wstring readyErr;
    const bool isReady = WaitForAiPageReady(pr, ready, &readyErr);
    LogTitleSample(L"workflow_after_page_ready", browser.hwnd,
                   isReady ? ready.detail : readyErr);
    if (!isReady) {
        QP_LOG_WARN(L"workflow: page not confirmed ready (%s) waited=%dms title='%s'",
                    readyErr.c_str(), ready.waitedMs, ready.title.c_str());
        if (!cfg_.pasteEvenIfNotReady) {
            restoreClip();
            return fail(readyErr.empty() ? L"page not ready" : readyErr);
        }
        QP_LOG_WARN(L"workflow: pasting anyway (pasteEvenIfNotReady=1)");
    } else {
        QP_LOG_INFO(L"workflow: page ready in %dms via %s edit='%s' title='%s'",
                    ready.waitedMs, ready.detail.c_str(), ready.editName.c_str(),
                    ready.title.c_str());
    }

    ActivateBrowser(browser, nullptr);
    Sleep(40);
    LogForegroundTitle(L"workflow_before_paste");
    LogTitleSample(L"workflow_before_paste_browser", browser.hwnd, ready.title);

    QP_LOG_INFO(L"workflow: paste payload into AI input (%zu wchar)", payload.size());
    ReleaseModifiers(nullptr);
    WaitModifiersReleased(200);

    // Keep payload on clipboard until after paste settles — write immediately before Ctrl+V.
    if (!ClipboardWriteUnicode(payload, error)) {
        restoreClip();
        return fail(error && !error->empty() ? *error : L"clipboard payload write failed");
    }

    // Verify clipboard still holds payload (guards against races with other apps).
    {
        std::wstring verify;
        if (ClipboardReadUnicode(verify, nullptr)) {
            if (verify != payload) {
                QP_LOG_ERROR(L"workflow: clipboard verify mismatch (got %zu want %zu) — rewriting",
                             verify.size(), payload.size());
                ClipboardWriteUnicode(payload, nullptr);
            } else {
                QP_LOG_DEBUG(L"workflow: clipboard verify ok (%zu wchar)", verify.size());
            }
        }
    }

    if (!SendPaste(error)) {
        QP_LOG_WARN(L"workflow: Ctrl+V failed, trying unicode fallback");
        if (!SendUnicodeText(payload, error)) {
            restoreClip();
            return fail(error && !error->empty() ? *error : L"payload paste failed");
        }
    }
    if (cfg_.afterFinalPasteMs > 0) {
        Sleep(static_cast<DWORD>(cfg_.afterFinalPasteMs));
    }
    if (cfg_.clipboardRestoreDelayMs > 0) {
        Sleep(static_cast<DWORD>(cfg_.clipboardRestoreDelayMs));
    }

    LogTitleSample(L"workflow_after_paste", browser.hwnd);
    LogForegroundTitle(L"workflow_done");

    restoreClip();
    QP_LOG_DEBUG(L"workflow: user clipboard restored");
    QP_LOG_INFO(L"workflow: DONE SendToAi");
    return true;
}

} // namespace qp
