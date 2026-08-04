#include "workflow.hpp"
#include "browser.hpp"
#include "clipboard_image.hpp"
#include "ext_bridge.hpp"
#include "input_sim.hpp"
#include "logger.hpp"
#include "page_ready.hpp"
#include "title_sample.hpp"
#include "util.hpp"

#include <algorithm>

namespace qp
{

namespace
{

std::wstring PayloadPreview(const std::wstring& s, size_t maxChars = 160)
{
    std::wstring out;
    out.reserve(maxChars + 8);
    for (size_t i = 0; i < s.size() && out.size() < maxChars; ++i)
    {
        const wchar_t c = s[i];
        if (c == L'\r')
            continue;
        if (c == L'\n')
            out += L"↵";
        else
            out.push_back(c);
    }
    if (s.size() > maxChars)
        out += L"…";
    return out;
}

bool LooksLikeBrowserClass(const std::wstring& cls)
{
    return cls == L"Chrome_WidgetWin_1" || cls == L"Chrome_WidgetWin_0" ||
           cls == L"MozillaWindowClass";
}

bool ContainsI(const std::wstring& hay, const std::wstring& needle)
{
    if (needle.empty())
        return true;
    auto lower = [](std::wstring s) {
        for (auto& c : s)
            c = static_cast<wchar_t>(towlower(c));
        return s;
    };
    return lower(hay).find(lower(needle)) != std::wstring::npos;
}

// Extension/UIA failures that mean "abort entirely" — do not surprise-paste via fallback.
bool IsHardPasteCancel(const std::wstring& msg)
{
    if (msg.empty())
        return false;
    static const wchar_t* kKeys[] = {
        L"focus switched",
        L"focus/title switched",
        L"not focused",
        L"tab inactive",
        L"tab hidden",
        L"document hidden",
        L"cancelled",
        L"canceled",
        L"composer not found",
        L"timed out",
        L"timeout",
        L"page not ready",
        L"paste left duplicated",
        L"could not be repaired",
    };
    for (const wchar_t* k : kKeys)
    {
        if (ContainsI(msg, k))
            return true;
    }
    return false;
}

std::wstring WindowTitleHwnd(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return {};
    wchar_t buf[512]{};
    GetWindowTextW(hwnd, buf, 512);
    return buf;
}

// Pre-paste guard for UIA path: FG must still be the AI browser/tab.
bool ForegroundOkForPaste(HWND browserHwnd, const std::wstring& titleHint, std::wstring* why)
{
    if (!browserHwnd || !IsWindow(browserHwnd))
    {
        if (why)
            *why = L"browser window disappeared before paste";
        return false;
    }
    HWND fg = GetForegroundWindow();
    if (!fg)
        return true;

    auto isUnder = [](HWND ancestor, HWND hwnd) {
        for (HWND w = hwnd; w; w = GetParent(w))
        {
            if (w == ancestor)
                return true;
        }
        return false;
    };

    const std::wstring brTitle = WindowTitleHwnd(browserHwnd);
    if (!titleHint.empty() && !ContainsI(brTitle, titleHint))
    {
        // Allow bare browser chrome while loading is rare at paste time — still cancel.
        if (why)
            *why = L"focus/title switched off AI page (browser title='" + brTitle + L"')";
        return false;
    }

    if (fg == browserHwnd || isUnder(browserHwnd, fg))
        return true;

    DWORD fgPid = 0, brPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    GetWindowThreadProcessId(browserHwnd, &brPid);
    const std::wstring fgTitle = WindowTitleHwnd(fg);
    if (fgPid != 0 && brPid != 0 && fgPid != brPid)
    {
        if (why)
            *why = L"focus switched to other window title='" + fgTitle + L"'";
        return false;
    }
    if (!titleHint.empty() && !ContainsI(fgTitle, titleHint))
    {
        if (why)
            *why = L"focus switched to other tab/window title='" + fgTitle + L"'";
        return false;
    }
    return true;
}

} // namespace

AiWorkflow::AiWorkflow(const WorkflowConfig& cfg) : cfg_(cfg)
{
}

std::wstring AiWorkflow::ComposePayload(const std::wstring& promptBody,
                                        const std::wstring& editorText, bool fenceEditorText)
{
    return BuildPromptPayload(promptBody, editorText, fenceEditorText);
}

bool AiWorkflow::Run(const std::wstring& promptBody, const std::wstring& aiUrlOverride,
                     std::wstring* error)
{
    WorkflowRequest req;
    req.promptBody = promptBody;
    req.aiUrl = aiUrlOverride;
    req.captureEditor = true;
    req.fenceEditorText = cfg_.fenceEditorText;
    return Run(req, error);
}

bool AiWorkflow::Run(const WorkflowRequest& req, std::wstring* error)
{
    QP_LOG_INFO(L"workflow: BEGIN service=%s label=%s capture=%d image=%d", req.service.c_str(),
                req.label.c_str(), req.captureEditor ? 1 : 0, req.requireClipboardImage ? 1 : 0);

    auto fail = [&](const std::wstring& msg) -> bool {
        QP_LOG_ERROR(L"workflow: FAIL %s", msg.c_str());
        if (error)
            *error = msg;
        return false;
    };

    const std::wstring url = !req.aiUrl.empty() ? req.aiUrl : cfg_.defaultAiUrl;
    if (url.empty())
        return fail(L"AI URL is empty");
    if (req.promptBody.empty())
        return fail(L"prompt template is empty");

    EnsureComInitialized();

    // Snapshot image early (navigate destroys clipboard).
    ClipboardImage savedImage;
    if (req.requireClipboardImage)
    {
        if (!ClipboardHasImage())
        {
            return fail(L"screenshot binding requires an image on the clipboard "
                        L"(Win+Shift+S, then trigger the hotkey)");
        }
        std::wstring ierr;
        if (!ClipboardSaveImage(savedImage, &ierr))
        {
            return fail(ierr.empty() ? L"failed to save clipboard image" : ierr);
        }
        QP_LOG_INFO(L"workflow: clipboard image saved (png=%d dib=%d)", savedImage.hasPng ? 1 : 0,
                    savedImage.hasDib ? 1 : 0);
    }

    ReleaseModifiers(nullptr);
    WaitModifiersReleased(500);
    if (cfg_.afterModifierReleaseMs > 0)
    {
        Sleep(static_cast<DWORD>(cfg_.afterModifierReleaseMs));
    }

    std::wstring userClipText;
    ClipboardReadUnicode(userClipText, nullptr);

    auto restoreClip = [&]() {
        // Prefer restoring image if we stole one; else prior text.
        if (!savedImage.empty())
        {
            ClipboardRestoreImage(savedImage, nullptr);
        } else if (!userClipText.empty())
        {
            ClipboardWriteUnicode(userClipText, nullptr);
        }
    };

    std::wstring editorText;
    if (req.captureEditor)
    {
        const FocusSnapshot source = CaptureFocusSnapshot();
        if (!source.foreground)
        {
            return fail(L"no foreground window to capture from");
        }
        LogTitleSample(L"workflow_source_editor", source.foreground, source.fgClass);
        if (LooksLikeBrowserClass(source.fgClass))
        {
            QP_LOG_WARN(L"workflow: source is a browser — Ctrl+A/C copies the page. "
                        L"Focus your text document first if that was intended.");
        }

        QP_LOG_INFO(L"workflow: select-all + copy");
        if (!SendSelectAll(error))
            return fail(error && !error->empty() ? *error : L"Ctrl+A failed");
        if (cfg_.afterSelectAllMs > 0)
            Sleep(static_cast<DWORD>(cfg_.afterSelectAllMs));
        if (!SendCopy(error))
            return fail(error && !error->empty() ? *error : L"Ctrl+C failed");
        if (cfg_.afterCopyMs > 0)
            Sleep(static_cast<DWORD>(cfg_.afterCopyMs));

        if (!ClipboardReadUnicode(editorText, error))
        {
            return fail(error && !error->empty() ? *error : L"clipboard read failed");
        }
        QP_LOG_INFO(L"workflow: captured text (%zu wchar) preview='%s'", editorText.size(),
                    PayloadPreview(editorText, 80).c_str());
    } else
    {
        QP_LOG_INFO(L"workflow: skip editor capture");
    }

    const bool fence = req.fenceEditorText;
    // userClipText was snapshotted before capture — that is the intended {{CONTEXT}}
    // (select context on a page → Ctrl+C → focus draft → hotkey).
    if (!userClipText.empty() && (req.promptBody.find(L"{{CONTEXT}}") != std::wstring::npos))
    {
        // INFO: length only. DEBUG may preview clipboard text (often sensitive).
        QP_LOG_INFO(L"workflow: context from pre-capture clipboard (%zu wchar)",
                    userClipText.size());
        QP_LOG_TRACE(L"workflow: context clipboard preview='%s'",

                     PayloadPreview(userClipText, 80).c_str());
    }
    const std::wstring payload =
        BuildPromptPayload(req.promptBody, editorText, fence, userClipText);
    // Payload can embed {{CONTEXT}} (pre-capture clipboard) — never preview at INFO.
    QP_LOG_INFO(L"workflow: payload %zu wchar fence=%d", payload.size(), fence ? 1 : 0);
    QP_LOG_DEBUG(L"workflow: payload preview='%s'", PayloadPreview(payload, 200).c_str());

    // --- Extension path (DOM) when companion is connected ---
    // Image paste still needs clipboard + browser focus (extension text-only for now).
    if (cfg_.preferExtension && ExtBridge::Instance().IsExtensionReady() &&
        !req.requireClipboardImage)
    {
        QP_LOG_INFO(L"workflow: trying Chrome extension prepareAndPaste");
        std::wstring detail;
        std::wstring extErr;
        // Cap at page-ready timeout (default 10s). No +5s cushion — long waits → surprise paste.
        const DWORD extTimeout =
            static_cast<DWORD>((std::max)(1000, (std::min)(cfg_.pageReadyTimeoutMs, 60000)));
        if (ExtBridge::Instance().PrepareAndPaste(url, payload, extTimeout, &detail, &extErr))
        {
            QP_LOG_INFO(L"workflow: extension paste OK (%s)", detail.c_str());
            // Extension set the composer via DOM — no clipboard hold required for SPA race.
            // Still restore user clip (we may have touched it for editor capture only).
            if (cfg_.afterFinalPasteMs > 0)
                Sleep(static_cast<DWORD>(cfg_.afterFinalPasteMs));
            restoreClip();
            QP_LOG_INFO(L"workflow: DONE (extension)");
            return true;
        }
        const std::wstring why = !extErr.empty() ? extErr : detail;
        // Timeout / focus-leave / missing form: abort. Do not UIA-paste onto whatever is focused.
        if (IsHardPasteCancel(why))
        {
            restoreClip();
            return fail(L"extension paste cancelled: " + why);
        }
        QP_LOG_WARN(L"workflow: extension path failed (%s) — falling back to UIA", why.c_str());
    } else if (cfg_.preferExtension)
    {
        QP_LOG_DEBUG(L"workflow: extension not connected — UIA path");
    }

    BrowserTarget browser;
    if (!FindBrowserWindow(cfg_.browserTitleHint, browser, error))
    {
        restoreClip();
        return false;
    }
    LogTitleSample(L"workflow_browser_selected", browser.hwnd, browser.title);
    if (!ActivateBrowser(browser, error))
    {
        QP_LOG_WARN(L"workflow: ActivateBrowser weak focus — continuing");
    }
    if (cfg_.afterActivateBrowserMs > 0)
    {
        Sleep(static_cast<DWORD>(cfg_.afterActivateBrowserMs));
    }

    QP_LOG_INFO(L"workflow: new tab");
    ReleaseModifiers(nullptr);
    if (!SendNewTab(error))
    {
        restoreClip();
        return fail(error && !error->empty() ? *error : L"Ctrl+T failed");
    }
    if (cfg_.afterNewTabMs > 0)
        Sleep(static_cast<DWORD>(cfg_.afterNewTabMs));

    QP_LOG_INFO(L"workflow: navigate to %s", url.c_str());
    SendFocusOmnibox(nullptr);
    Sleep(40);
    if (!ClipboardWriteUnicode(url, error))
    {
        restoreClip();
        return fail(error && !error->empty() ? *error : L"clipboard url write failed");
    }
    if (!SendPaste(error))
    {
        restoreClip();
        return fail(error && !error->empty() ? *error : L"paste URL failed");
    }
    if (cfg_.afterUrlPasteMs > 0)
        Sleep(static_cast<DWORD>(cfg_.afterUrlPasteMs));
    if (!SendEnter(error))
    {
        restoreClip();
        return fail(error && !error->empty() ? *error : L"Enter failed");
    }
    LogTitleSample(L"workflow_after_navigate_enter", browser.hwnd, url);

    {
        BrowserTarget b2;
        if (FindBrowserWindow(cfg_.browserTitleHint, b2, nullptr) && b2.hwnd)
        {
            browser = b2;
        }
    }

    PageReadyConfig pr;
    pr.browserHwnd = browser.hwnd;
    if (!req.pageTitleHint.empty())
        pr.titleHint = req.pageTitleHint;
    else if (!cfg_.pageTitleHint.empty())
        pr.titleHint = cfg_.pageTitleHint;
    else
        pr.titleHint = TitleHintFromUrl(url);
    pr.timeoutMs = cfg_.pageReadyTimeoutMs;
    pr.pollMs = cfg_.pageReadyPollMs;
    pr.minWaitMs = cfg_.pageReadyMinMs;
    pr.settleMs = cfg_.pageReadySettleMs;
    pr.useUia = cfg_.pageReadyUseUia;
    pr.cancelOnFocusSwitch = cfg_.cancelOnFocusSwitch;

    QP_LOG_INFO(L"workflow: page-ready hint='%s' timeout=%dms cancelOnFocus=%d",
                pr.titleHint.c_str(), pr.timeoutMs, pr.cancelOnFocusSwitch ? 1 : 0);
    PageReadyResult ready{};
    std::wstring readyErr;
    const bool isReady = WaitForAiPageReady(pr, ready, &readyErr);
    if (!isReady)
    {
        QP_LOG_WARN(L"workflow: page not ready (%s)", readyErr.c_str());
        // Focus-switch / hard cancel always aborts even if pasteEvenIfNotReady is on.
        if (!cfg_.pasteEvenIfNotReady || IsHardPasteCancel(readyErr))
        {
            restoreClip();
            return fail(readyErr.empty() ? L"page not ready" : readyErr);
        }
    } else
    {
        QP_LOG_INFO(L"workflow: page ready in %dms (%s) title='%s'", ready.waitedMs,
                    ready.detail.c_str(), ready.title.c_str());
    }

    ActivateBrowser(browser, nullptr);
    Sleep(40);
    ReleaseModifiers(nullptr);
    WaitModifiersReleased(200);

    if (cfg_.cancelOnFocusSwitch)
    {
        std::wstring focusWhy;
        if (!ForegroundOkForPaste(browser.hwnd, pr.titleHint, &focusWhy))
        {
            restoreClip();
            return fail(focusWhy.empty() ? L"focus switched before paste" : focusWhy);
        }
    }

    // --- Paste into AI form ---
    // Clipboard+Ctrl+V keeps newlines/formatting (human-readable).
    // Meta may re-read the clipboard after Ctrl+V — so we KEEP the payload on
    // the clipboard for clipboardRestoreDelayMs before restoring the user clip.
    // Unicode path is fallback; it maps \n → VK_RETURN so lines still break.
    if (req.requireClipboardImage && !savedImage.empty())
    {
        QP_LOG_INFO(L"workflow: paste image first");
        std::wstring ierr;
        if (!ClipboardRestoreImage(savedImage, &ierr))
        {
            restoreClip();
            return fail(ierr.empty() ? L"restore image failed" : ierr);
        }
        if (!SendPaste(error))
        {
            restoreClip();
            return fail(error && !error->empty() ? *error : L"paste image failed");
        }
        if (cfg_.afterImagePasteMs > 0)
        {
            Sleep(static_cast<DWORD>(cfg_.afterImagePasteMs));
        }
        // Image consumed; clear so restoreClip won't re-put image over text path.
        savedImage = {};
    }

    bool usedClipboardForText = false;
    QP_LOG_INFO(L"workflow: deliver text payload (%zu wchar) via=%s", payload.size(),
                cfg_.pasteTextViaUnicode ? L"unicode" : L"clipboard");
    QP_LOG_DEBUG(L"workflow: deliver text payload preview='%s'",
                 PayloadPreview(payload, 120).c_str());

    if (cfg_.pasteTextViaUnicode)
    {
        // Direct typing into focused composer — clipboard untouched.
        if (!SendUnicodeText(payload, error))
        {
            QP_LOG_WARN(L"workflow: unicode type failed — falling back to clipboard paste");
            if (!ClipboardWriteUnicode(payload, error))
            {
                restoreClip();
                return fail(error && !error->empty() ? *error : L"clipboard payload write failed");
            }
            usedClipboardForText = true;
            if (!SendPaste(error))
            {
                restoreClip();
                return fail(error && !error->empty() ? *error : L"payload paste failed");
            }
        }
    } else
    {
        if (!ClipboardWriteUnicode(payload, error))
        {
            restoreClip();
            return fail(error && !error->empty() ? *error : L"clipboard payload write failed");
        }
        {
            std::wstring verify;
            if (ClipboardReadUnicode(verify, nullptr) && verify != payload)
            {
                QP_LOG_WARN(L"workflow: clipboard verify mismatch — rewrite");
                ClipboardWriteUnicode(payload, nullptr);
            }
        }
        if (!SendPaste(error))
        {
            QP_LOG_WARN(L"workflow: Ctrl+V text failed, unicode fallback");
            if (!SendUnicodeText(payload, error))
            {
                restoreClip();
                return fail(error && !error->empty() ? *error : L"payload paste failed");
            }
        } else
        {
            usedClipboardForText = true;
        }
    }

    if (cfg_.afterFinalPasteMs > 0)
        Sleep(static_cast<DWORD>(cfg_.afterFinalPasteMs));

    // Only wait to restore clipboard if we actually used it for the text paste
    // (or still hold a saved image). Otherwise restore immediately is fine.
    if (usedClipboardForText || !savedImage.empty())
    {
        const int delay = (std::max)(cfg_.clipboardRestoreDelayMs, 1500);
        QP_LOG_INFO(L"workflow: holding clipboard %dms before restore (usedClipText=%d)", delay,
                    usedClipboardForText ? 1 : 0);
        // Keep PAYLOAD on clipboard during the hold so a late SPA read still
        // gets the full prompt+text, not the user's previous clip.
        if (usedClipboardForText)
        {
            ClipboardWriteUnicode(payload, nullptr);
        }
        Sleep(static_cast<DWORD>(delay));
    }

    if (!userClipText.empty())
    {
        QP_LOG_DEBUG(L"workflow: restoring user clipboard (%zu wchar) preview='%s'",
                     userClipText.size(), PayloadPreview(userClipText, 60).c_str());
    }
    restoreClip();
    QP_LOG_INFO(L"workflow: DONE");
    return true;
}

} // namespace qp
