#include "workflow.hpp"
#include "browser.hpp"
#include "input_sim.hpp"
#include "logger.hpp"
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

    // --- 0) Clear hotkey modifiers ---
    ReleaseModifiers(nullptr);
    WaitModifiersReleased(500);
    if (cfg_.afterModifierReleaseMs > 0) {
        Sleep(static_cast<DWORD>(cfg_.afterModifierReleaseMs));
    }

    // Snapshot source editor (for logs)
    const FocusSnapshot source = CaptureFocusSnapshot();
    if (!source.foreground) {
        return fail(L"no foreground window to capture from");
    }

    // Save user clipboard to restore at end
    std::wstring userClip;
    ClipboardReadUnicode(userClip, nullptr);

    // --- 1) Select all + copy from editor ---
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

    // Compose final payload
    std::wstring payload = promptBody;
    if (!editorText.empty()) {
        // Templates already end with \n\n typically; still separate clearly.
        if (!payload.empty() && payload.back() != L'\n') payload += L"\n\n";
        payload += editorText;
    }
    QP_LOG_DEBUG(L"workflow: payload %zu wchar (prompt %zu + editor %zu)",
                 payload.size(), promptBody.size(), editorText.size());

    // --- 2) Find + activate browser ---
    BrowserTarget browser;
    if (!FindBrowserWindow(cfg_.browserTitleHint, browser, error)) {
        // Restore clipboard before bailing
        if (!userClip.empty()) ClipboardWriteUnicode(userClip, nullptr);
        return false;
    }
    if (!ActivateBrowser(browser, error)) {
        QP_LOG_WARN(L"workflow: ActivateBrowser weak focus — continuing anyway");
    }
    if (cfg_.afterActivateBrowserMs > 0) {
        Sleep(static_cast<DWORD>(cfg_.afterActivateBrowserMs));
    }

    // --- 3) New tab ---
    QP_LOG_INFO(L"workflow: new tab");
    ReleaseModifiers(nullptr);
    if (!SendNewTab(error)) {
        if (!userClip.empty()) ClipboardWriteUnicode(userClip, nullptr);
        return fail(error && !error->empty() ? *error : L"Ctrl+T failed");
    }
    if (cfg_.afterNewTabMs > 0) Sleep(static_cast<DWORD>(cfg_.afterNewTabMs));

    // --- 4) Navigate to AI URL (omnibox should be focused on new tab) ---
    QP_LOG_INFO(L"workflow: navigate to %s", url.c_str());
    // Focus omnibox explicitly (helps if new-tab focus is flaky)
    SendFocusOmnibox(nullptr);
    Sleep(40);

    if (!ClipboardWriteUnicode(url, error)) {
        if (!userClip.empty()) ClipboardWriteUnicode(userClip, nullptr);
        return fail(error && !error->empty() ? *error : L"clipboard url write failed");
    }
    if (!SendPaste(error)) {
        if (!userClip.empty()) ClipboardWriteUnicode(userClip, nullptr);
        return fail(error && !error->empty() ? *error : L"paste URL failed");
    }
    if (cfg_.afterUrlPasteMs > 0) Sleep(static_cast<DWORD>(cfg_.afterUrlPasteMs));
    if (!SendEnter(error)) {
        if (!userClip.empty()) ClipboardWriteUnicode(userClip, nullptr);
        return fail(error && !error->empty() ? *error : L"Enter failed");
    }

    QP_LOG_INFO(L"workflow: waiting %d ms for page load / input focus",
                cfg_.afterNavigateMs);
    if (cfg_.afterNavigateMs > 0) {
        Sleep(static_cast<DWORD>(cfg_.afterNavigateMs));
    }

    // --- 5) Paste prompt + editor text into AI input ---
    QP_LOG_INFO(L"workflow: paste payload into AI input");
    ReleaseModifiers(nullptr);
    WaitModifiersReleased(200);

    if (!ClipboardWriteUnicode(payload, error)) {
        if (!userClip.empty()) ClipboardWriteUnicode(userClip, nullptr);
        return fail(error && !error->empty() ? *error : L"clipboard payload write failed");
    }
    if (!SendPaste(error)) {
        // Fallback: unicode type (slow but sometimes works if paste blocked)
        QP_LOG_WARN(L"workflow: Ctrl+V failed, trying unicode fallback");
        if (!SendUnicodeText(payload, error)) {
            if (!userClip.empty()) ClipboardWriteUnicode(userClip, nullptr);
            return fail(error && !error->empty() ? *error : L"payload paste failed");
        }
    }
    if (cfg_.afterFinalPasteMs > 0) {
        Sleep(static_cast<DWORD>(cfg_.afterFinalPasteMs));
    }

    // --- 6) Restore user clipboard ---
    if (!userClip.empty()) {
        if (!ClipboardWriteUnicode(userClip, nullptr)) {
            QP_LOG_WARN(L"workflow: could not restore user clipboard");
        } else {
            QP_LOG_DEBUG(L"workflow: user clipboard restored");
        }
    }

    QP_LOG_INFO(L"workflow: DONE SendToAi");
    return true;
}

} // namespace qp
