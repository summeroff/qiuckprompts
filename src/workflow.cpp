#include "workflow.hpp"
#include "browser.hpp"
#include "input_sim.hpp"
#include "logger.hpp"
#include "page_ready.hpp"
#include "title_sample.hpp"
#include "util.hpp"

namespace qp {

AiWorkflow::AiWorkflow(const WorkflowConfig& cfg) : cfg_(cfg) {}

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

    std::wstring userClip;
    ClipboardReadUnicode(userClip, nullptr);

    auto restoreClip = [&]() {
        if (!userClip.empty()) {
            ClipboardWriteUnicode(userClip, nullptr);
        }
    };

    QP_LOG_INFO(L"workflow: select-all + copy from editor");
    if (!SendSelectAll(error)) return fail(error && !error->empty() ? *error : L"Ctrl+A failed");
    if (cfg_.afterSelectAllMs > 0) Sleep(static_cast<DWORD>(cfg_.afterSelectAllMs));

    if (!SendCopy(error)) return fail(error && !error->empty() ? *error : L"Ctrl+C failed");
    if (cfg_.afterCopyMs > 0) Sleep(static_cast<DWORD>(cfg_.afterCopyMs));

    std::wstring editorText;
    if (!ClipboardReadUnicode(editorText, error)) {
        return fail(error && !error->empty() ? *error : L"clipboard read failed");
    }
    QP_LOG_INFO(L"workflow: captured editor text (%zu wchar)", editorText.size());
    if (editorText.empty()) {
        QP_LOG_WARN(L"workflow: editor text empty — continuing with prompt only");
    }

    std::wstring payload = promptBody;
    if (!editorText.empty()) {
        if (!payload.empty() && payload.back() != L'\n') payload += L"\n\n";
        payload += editorText;
    }
    QP_LOG_DEBUG(L"workflow: payload %zu wchar (prompt %zu + editor %zu)",
                 payload.size(), promptBody.size(), editorText.size());

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

    QP_LOG_INFO(L"workflow: paste payload into AI input");
    ReleaseModifiers(nullptr);
    WaitModifiersReleased(200);

    if (!ClipboardWriteUnicode(payload, error)) {
        restoreClip();
        return fail(error && !error->empty() ? *error : L"clipboard payload write failed");
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

    LogTitleSample(L"workflow_after_paste", browser.hwnd);
    LogForegroundTitle(L"workflow_done");

    restoreClip();
    QP_LOG_DEBUG(L"workflow: user clipboard restored");
    QP_LOG_INFO(L"workflow: DONE SendToAi");
    return true;
}

} // namespace qp
