#include "page_ready.hpp"
#include "logger.hpp"
#include "title_sample.hpp"
#include "util.hpp"

#include <ole2.h>
#include <UIAutomation.h>

#include <algorithm>
#include <vector>

namespace qp
{

namespace
{

// ---- COM / UIA lifetime ----------------------------------------------------

struct ComInit
{
    bool ok = false;
    HRESULT hr = E_FAIL;
    ComInit()
    {
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // S_OK first init, S_FALSE already initialized on this thread
        ok = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
        if (hr == RPC_E_CHANGED_MODE)
        {
            // Different model already — still try to use COM on this thread.
            ok = true;
            QP_LOG_WARN(L"com: CoInitializeEx RPC_E_CHANGED_MODE — continuing");
        } else if (!SUCCEEDED(hr) && hr != S_FALSE)
        {
            QP_LOG_ERROR(L"com: CoInitializeEx failed hr=0x%08X", static_cast<unsigned>(hr));
        }
    }
};

ComInit& Com()
{
    static ComInit c;
    return c;
}

std::wstring BstrToW(BSTR b)
{
    if (!b)
        return {};
    return std::wstring(b, SysStringLen(b));
}

std::wstring ToLowerCopy(std::wstring s)
{
    for (auto& ch : s)
        ch = static_cast<wchar_t>(towlower(ch));
    return s;
}

bool ContainsI(const std::wstring& hay, const std::wstring& needle)
{
    if (needle.empty())
        return true;
    return ToLowerCopy(hay).find(ToLowerCopy(needle)) != std::wstring::npos;
}

bool EqualsI(const std::wstring& a, const std::wstring& b)
{
    return ToLowerCopy(a) == ToLowerCopy(b);
}

std::wstring WindowTitleOf(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return {};
    wchar_t buf[512]{};
    GetWindowTextW(hwnd, buf, 512);
    return buf;
}

bool LooksLikeNewTabTitle(const std::wstring& title)
{
    const std::wstring t = Trim(title);
    if (t.empty())
        return true;
    const std::wstring l = ToLowerCopy(t);
    if (l == L"new tab" || l == L"untitled" || l == L"about:blank")
        return true;
    if (l.find(L"new tab") != std::wstring::npos)
        return true;
    // Just the browser chrome name with no page title yet
    if (l == L"google chrome" || l == L"chrome beta" || l == L"chrome dev" ||
        l == L"microsoft edge" || l == L"chromium")
    {
        return true;
    }
    return false;
}

bool IsAncestorOrSelf(HWND ancestor, HWND hwnd)
{
    if (!ancestor || !hwnd)
        return false;
    for (HWND w = hwnd; w; w = GetParent(w))
    {
        if (w == ancestor)
            return true;
    }
    return false;
}

// User left the AI target: other app focused, or same browser but title no longer matches hint.
// Returns empty if still OK; otherwise a short cancel reason for logs/errors.
std::wstring FocusSwitchCancelReason(HWND browserHwnd, const std::wstring& titleHint,
                                     bool sawTargetTitle)
{
    if (!browserHwnd || !IsWindow(browserHwnd))
        return L"browser window disappeared";

    const std::wstring browserTitle = WindowTitleOf(browserHwnd);
    // Tab switched away inside the same browser window after we had already seen the AI title.
    if (sawTargetTitle && !titleHint.empty() && !LooksLikeNewTabTitle(browserTitle) &&
        !ContainsI(browserTitle, titleHint))
    {
        return L"focus/title switched off AI page (browser title='" + browserTitle + L"')";
    }

    HWND fg = GetForegroundWindow();
    if (!fg)
        return {};

    if (IsAncestorOrSelf(browserHwnd, fg) || fg == browserHwnd)
        return {};

    DWORD fgPid = 0;
    DWORD brPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    GetWindowThreadProcessId(browserHwnd, &brPid);

    const std::wstring fgTitle = WindowTitleOf(fg);

    // Different process entirely (Hermes, Slack, game, …).
    if (fgPid != 0 && brPid != 0 && fgPid != brPid)
    {
        return L"focus switched to other window title='" + fgTitle + L"'";
    }

    // Same browser process, other window/tab in foreground without our AI hint.
    if (!titleHint.empty() && !ContainsI(fgTitle, titleHint) && !LooksLikeNewTabTitle(fgTitle))
    {
        return L"focus switched to other tab/window title='" + fgTitle + L"'";
    }

    return {};
}

bool IsOmniboxName(const std::wstring& name)
{
    const std::wstring l = ToLowerCopy(name);
    if (l.empty())
        return false;
    if (l.find(L"address") != std::wstring::npos)
        return true;
    if (l.find(L"search bar") != std::wstring::npos)
        return true;
    if (l.find(L"omnibox") != std::wstring::npos)
        return true;
    if (l.find(L"find in page") != std::wstring::npos)
        return true;
    if (l.find(L"find bar") != std::wstring::npos)
        return true;
    // Chrome sometimes: "Address and search bar"
    return false;
}

bool GetBoolProp(IUIAutomationElement* el, PROPERTYID id, bool defaultVal)
{
    VARIANT v;
    VariantInit(&v);
    const HRESULT hr = el->GetCurrentPropertyValue(id, &v);
    bool result = defaultVal;
    if (SUCCEEDED(hr) && v.vt == VT_BOOL)
    {
        result = (v.boolVal == VARIANT_TRUE);
    }
    VariantClear(&v);
    return result;
}

std::wstring GetStringProp(IUIAutomationElement* el, PROPERTYID id)
{
    VARIANT v;
    VariantInit(&v);
    std::wstring out;
    if (SUCCEEDED(el->GetCurrentPropertyValue(id, &v)) && v.vt == VT_BSTR && v.bstrVal)
    {
        out = BstrToW(v.bstrVal);
    }
    VariantClear(&v);
    return out;
}

int GetIntProp(IUIAutomationElement* el, PROPERTYID id, int defaultVal)
{
    VARIANT v;
    VariantInit(&v);
    int result = defaultVal;
    if (SUCCEEDED(el->GetCurrentPropertyValue(id, &v)) && v.vt == VT_I4)
    {
        result = v.lVal;
    }
    VariantClear(&v);
    return result;
}

bool IsPromisingEdit(IUIAutomationElement* el, std::wstring* nameOut)
{
    if (!el)
        return false;

    const int ctype = GetIntProp(el, UIA_ControlTypePropertyId, 0);
    const bool isEdit = (ctype == UIA_EditControlTypeId);
    const bool isDoc = (ctype == UIA_DocumentControlTypeId);
    if (!isEdit && !isDoc)
        return false;

    if (!GetBoolProp(el, UIA_IsEnabledPropertyId, false))
        return false;
    // Offscreen edits are usually hidden chrome / inactive tabs
    if (GetBoolProp(el, UIA_IsOffscreenPropertyId, true))
        return false;

    const std::wstring name = GetStringProp(el, UIA_NamePropertyId);
    if (IsOmniboxName(name))
        return false;

    // Documents that are the whole page are sometimes ControlType_Document;
    // only accept if keyboard focusable (chat composers usually are).
    if (isDoc && !GetBoolProp(el, UIA_IsKeyboardFocusablePropertyId, false))
    {
        return false;
    }

    if (nameOut)
        *nameOut = name;
    return true;
}

IUIAutomation* CreateAutomation(std::wstring* error)
{
    if (!Com().ok)
    {
        if (error)
            *error = L"COM not initialized";
        return nullptr;
    }
    IUIAutomation* auto_ = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_IUIAutomation, reinterpret_cast<void**>(&auto_));
    if (FAILED(hr) || !auto_)
    {
        if (error)
        {
            wchar_t buf[64];
            swprintf(buf, 64, L"CoCreateInstance UIAutomation failed hr=0x%08X",
                     static_cast<unsigned>(hr));
            *error = buf;
        }
        return nullptr;
    }
    return auto_;
}

bool TryFocusedEdit(IUIAutomation* automation, bool setFocus, std::wstring* editName,
                    std::wstring* error)
{
    IUIAutomationElement* focused = nullptr;
    HRESULT hr = automation->GetFocusedElement(&focused);
    if (FAILED(hr) || !focused)
    {
        if (error)
            *error = L"GetFocusedElement failed";
        return false;
    }

    std::wstring name;
    const bool ok = IsPromisingEdit(focused, &name);
    if (ok)
    {
        if (editName)
            *editName = name;
        if (setFocus)
        {
            focused->SetFocus();
        }
        QP_LOG_DEBUG(L"uia: focused element is usable edit name='%s'", name.c_str());
    } else
    {
        const int ctype = GetIntProp(focused, UIA_ControlTypePropertyId, 0);
        const std::wstring n = GetStringProp(focused, UIA_NamePropertyId);
        QP_LOG_TRACE(L"uia: focused not usable edit ctype=%d name='%s'", ctype, n.c_str());
    }
    focused->Release();
    return ok;
}

bool TryFindEditInTree(IUIAutomation* automation, HWND hwnd, bool setFocus, std::wstring* editName,
                       std::wstring* error)
{
    IUIAutomationElement* root = nullptr;
    HRESULT hr = automation->ElementFromHandle(hwnd, &root);
    if (FAILED(hr) || !root)
    {
        if (error)
            *error = L"ElementFromHandle failed";
        return false;
    }

    // Condition: ControlType == Edit  OR  later we also try Document.
    auto findByType = [&](long controlTypeId) -> IUIAutomationElement* {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = controlTypeId;

        IUIAutomationCondition* cond = nullptr;
        if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &cond)) ||
            !cond)
        {
            VariantClear(&v);
            return nullptr;
        }
        VariantClear(&v);

        IUIAutomationElementArray* arr = nullptr;
        // Descendants can be large; cap by scanning array for first promising hit.
        hr = root->FindAll(TreeScope_Descendants, cond, &arr);
        cond->Release();
        if (FAILED(hr) || !arr)
            return nullptr;

        int len = 0;
        arr->get_Length(&len);
        IUIAutomationElement* found = nullptr;
        for (int i = 0; i < len; ++i)
        {
            IUIAutomationElement* el = nullptr;
            if (FAILED(arr->GetElement(i, &el)) || !el)
                continue;
            std::wstring name;
            if (IsPromisingEdit(el, &name))
            {
                // Prefer keyboard-focusable named fields (chat boxes often have a name/placeholder).
                const bool focusable = GetBoolProp(el, UIA_IsKeyboardFocusablePropertyId, false);
                if (!found)
                {
                    found = el; // keep first candidate
                    if (editName)
                        *editName = name;
                    // Prefer focusable — if this one is, stop; else keep looking for better.
                    if (focusable)
                        break;
                    continue; // don't release; held as found
                }
                // Have a found already — upgrade if new one is focusable and old wasn't
                const bool oldFocusable =
                    GetBoolProp(found, UIA_IsKeyboardFocusablePropertyId, false);
                if (!oldFocusable && focusable)
                {
                    found->Release();
                    found = el;
                    if (editName)
                        *editName = name;
                    break;
                }
                el->Release();
            } else
            {
                el->Release();
            }
        }
        arr->Release();
        return found;
    };

    IUIAutomationElement* edit = findByType(UIA_EditControlTypeId);
    if (!edit)
    {
        QP_LOG_TRACE(L"uia: no Edit control; trying Document");
        edit = findByType(UIA_DocumentControlTypeId);
    }

    root->Release();

    if (!edit)
    {
        if (error)
            *error = L"no suitable Edit/Document in UIA tree";
        return false;
    }

    std::wstring name = editName ? *editName : GetStringProp(edit, UIA_NamePropertyId);
    if (editName && editName->empty())
        *editName = name;

    if (setFocus)
    {
        const HRESULT fhr = edit->SetFocus();
        QP_LOG_DEBUG(L"uia: SetFocus on edit name='%s' hr=0x%08X", name.c_str(),
                     static_cast<unsigned>(fhr));
        // Small yield so focus settles
        Sleep(30);
    } else
    {
        QP_LOG_DEBUG(L"uia: found edit name='%s' (no focus)", name.c_str());
    }

    edit->Release();
    return true;
}

bool TitleLooksReady(const std::wstring& title, const std::wstring& hint)
{
    if (LooksLikeNewTabTitle(title))
        return false;
    if (!hint.empty() && !ContainsI(title, hint))
        return false;
    return true;
}

} // namespace

bool EnsureComInitialized()
{
    return Com().ok;
}

std::wstring TitleHintFromUrl(const std::wstring& url)
{
    const std::wstring u = ToLowerCopy(url);
    if (u.find(L"meta.ai") != std::wstring::npos)
        return L"Meta";
    if (u.find(L"gemini.google") != std::wstring::npos)
        return L"Gemini";
    if (u.find(L"chatgpt.com") != std::wstring::npos ||
        u.find(L"chat.openai.com") != std::wstring::npos)
        return L"ChatGPT";
    if (u.find(L"claude.ai") != std::wstring::npos)
        return L"Claude";
    if (u.find(L"grok.x.ai") != std::wstring::npos || u.find(L"grok.com") != std::wstring::npos)
        return L"Grok";
    if (u.find(L"copilot.microsoft") != std::wstring::npos)
        return L"Copilot";
    if (u.find(L"perplexity.ai") != std::wstring::npos)
        return L"Perplexity";
    return {};
}

bool UiaFindChatEdit(HWND browserHwnd, std::wstring* editName, bool setFocus, std::wstring* error)
{
    if (!browserHwnd || !IsWindow(browserHwnd))
    {
        if (error)
            *error = L"bad browser hwnd";
        return false;
    }
    IUIAutomation* automation = CreateAutomation(error);
    if (!automation)
        return false;

    // Prefer already-focused edit (common once SPA mounts the composer).
    std::wstring name;
    bool ok = TryFocusedEdit(automation, setFocus, &name, nullptr);
    if (!ok)
    {
        ok = TryFindEditInTree(automation, browserHwnd, setFocus, &name, error);
    }
    if (ok && editName)
        *editName = name;
    automation->Release();
    return ok;
}

bool WaitForAiPageReady(const PageReadyConfig& cfg, PageReadyResult& out, std::wstring* error)
{
    out = {};
    if (!cfg.browserHwnd || !IsWindow(cfg.browserHwnd))
    {
        if (error)
            *error = L"WaitForAiPageReady: invalid browser hwnd";
        return false;
    }

    EnsureComInitialized();

    const DWORD start = GetTickCount();
    QP_LOG_INFO(L"page_ready: wait start timeout=%dms min=%dms hint='%s' uia=%d hwnd=%p",
                cfg.timeoutMs, cfg.minWaitMs, cfg.titleHint.c_str(), cfg.useUia ? 1 : 0,
                cfg.browserHwnd);

    IUIAutomation* automation = nullptr;
    if (cfg.useUia)
    {
        std::wstring uerr;
        automation = CreateAutomation(&uerr);
        if (!automation)
        {
            QP_LOG_WARN(L"page_ready: UIA unavailable (%s) — title-only fallback", uerr.c_str());
        }
    }

    bool titleReady = false;
    bool editReady = false;
    bool sawTargetTitle = false;
    std::wstring lastTitle;
    std::wstring prevLoggedTitle;
    int lastTreeScanMs = -10000;
    int lastTitleLogMs = -10000;

    // Initial title at wait start
    LogTitleSample(L"page_ready_start", cfg.browserHwnd, cfg.titleHint);

    for (;;)
    {
        const int elapsed = static_cast<int>(GetTickCount() - start);
        out.waitedMs = elapsed;

        if (!IsWindow(cfg.browserHwnd))
        {
            if (automation)
                automation->Release();
            if (error)
                *error = L"browser window disappeared";
            return false;
        }

        if (cfg.cancelOnFocusSwitch)
        {
            const std::wstring focusErr =
                FocusSwitchCancelReason(cfg.browserHwnd, cfg.titleHint, sawTargetTitle);
            if (!focusErr.empty())
            {
                if (automation)
                    automation->Release();
                out.ready = false;
                out.detail = focusErr;
                out.title = WindowTitleOf(cfg.browserHwnd);
                out.waitedMs = elapsed;
                QP_LOG_WARN(L"page_ready: CANCEL %s (t=%dms)", focusErr.c_str(), elapsed);
                LogTitleSample(L"page_ready_focus_cancel", cfg.browserHwnd, focusErr);
                if (error)
                    *error = focusErr;
                return false;
            }
        }

        lastTitle = WindowTitleOf(cfg.browserHwnd);
        out.title = lastTitle;
        titleReady = TitleLooksReady(lastTitle, cfg.titleHint);
        if (titleReady)
            sawTargetTitle = true;

        // Log every title *change*, and at least every ~1s while waiting.
        if (lastTitle != prevLoggedTitle || elapsed - lastTitleLogMs >= 1000)
        {
            wchar_t note[128];
            swprintf(note, 128, L"t=%dms titleReady=%d hint='%s'", elapsed, titleReady ? 1 : 0,
                     cfg.titleHint.c_str());
            LogTitleSample(L"page_ready_poll", cfg.browserHwnd, note);
            prevLoggedTitle = lastTitle;
            lastTitleLogMs = elapsed;
        }

        if (titleReady && automation)
        {
            std::wstring name;
            // Cheap path first: whatever currently has focus.
            if (cfg.preferFocusedEdit &&
                TryFocusedEdit(automation, cfg.focusFoundEdit, &name, nullptr))
            {
                editReady = true;
                out.focusedEdit = true;
                out.usedUia = true;
                out.editName = name;
            } else if (elapsed - lastTreeScanMs >= 400)
            {
                // Full tree scan is expensive on Chrome — throttle it.
                lastTreeScanMs = elapsed;
                if (TryFindEditInTree(automation, cfg.browserHwnd, cfg.focusFoundEdit, &name,
                                      nullptr))
                {
                    editReady = true;
                    out.usedUia = true;
                    out.editName = name;
                }
            }
        }

        const bool minElapsed = elapsed >= cfg.minWaitMs;
        const bool uiaOk = editReady;

        if (minElapsed && uiaOk)
        {
            out.ready = true;
            break;
        }

        // Title-only mode (UIA off or unavailable)
        if (minElapsed && titleReady && (!cfg.useUia || !automation))
        {
            out.ready = true;
            out.detail = L"title heuristic only";
            break;
        }

        // Soft fallback: title looks good for a while but no Edit found
        // (some SPAs use non-Edit control types).
        if (minElapsed && titleReady && elapsed >= (cfg.timeoutMs * 3) / 4)
        {
            QP_LOG_WARN(L"page_ready: title ready but no UIA edit after %d ms — proceeding",
                        elapsed);
            out.ready = true;
            out.detail = L"title ready; UIA edit not found (timeout soft)";
            break;
        }

        if (elapsed >= cfg.timeoutMs)
        {
            break;
        }

        if ((elapsed % 1000) < cfg.pollMs)
        {
            QP_LOG_DEBUG(L"page_ready: t=%dms titleReady=%d editReady=%d title='%s'", elapsed,
                         titleReady ? 1 : 0, editReady ? 1 : 0, lastTitle.c_str());
        }

        Sleep(static_cast<DWORD>(cfg.pollMs > 0 ? cfg.pollMs : 100));
    }

    if (automation)
        automation->Release();

    out.waitedMs = static_cast<int>(GetTickCount() - start);
    out.title = lastTitle;

    if (!out.ready)
    {
        out.detail = L"timeout waiting for page/input";
        QP_LOG_WARN(L"page_ready: TIMEOUT after %dms title='%s' titleReady=%d editReady=%d",
                    out.waitedMs, out.title.c_str(), titleReady ? 1 : 0, editReady ? 1 : 0);
        LogTitleSample(L"page_ready_timeout", cfg.browserHwnd, out.title);
        if (error)
        {
            *error = L"Timed out waiting for AI page/input. title='" + out.title + L"'";
        }
        // Still return false — caller may choose to paste anyway.
        return false;
    }

    if (out.detail.empty())
    {
        if (out.usedUia)
        {
            out.detail = out.focusedEdit ? L"focused UIA edit" : L"found UIA edit in tree";
        } else
        {
            out.detail = L"title heuristic only";
        }
    }

    QP_LOG_INFO(L"page_ready: READY after %dms (%s) title='%s' edit='%s'", out.waitedMs,
                out.detail.c_str(), out.title.c_str(), out.editName.c_str());
    LogTitleSample(L"page_ready_ready", cfg.browserHwnd, out.detail);

    if (cfg.settleMs > 0)
    {
        Sleep(static_cast<DWORD>(cfg.settleMs));
    }
    return true;
}

} // namespace qp
