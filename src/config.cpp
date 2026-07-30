#include "config.hpp"
#include "logger.hpp"
#include "version.hpp"

#include <fstream>
#include <map>
#include <sstream>

namespace qp
{

namespace
{

bool TakeEqValue(int& i, int argc, wchar_t** argv, const std::wstring& arg, const wchar_t* flag,
                 std::wstring& out)
{
    const std::wstring prefix = std::wstring(flag) + L"=";
    if (arg.rfind(prefix, 0) == 0)
    {
        out = arg.substr(prefix.size());
        return true;
    }
    if (arg == flag)
    {
        if (i + 1 < argc && argv[i + 1])
        {
            out = argv[++i];
            return true;
        }
    }
    return false;
}

int ClampInt(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

bool SplitKeyValue(const std::wstring& line, std::wstring& key, std::wstring& value)
{
    const size_t eq = line.find(L'=');
    if (eq == std::wstring::npos)
        return false;
    key = Trim(line.substr(0, eq));
    value = Trim(line.substr(eq + 1));
    return !key.empty();
}

bool ReadUtf8File(const std::wstring& path, std::wstring& out, std::wstring* error)
{
    out.clear();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
    {
        if (error)
            *error = L"cannot open " + path;
        return false;
    }
    std::string bytes;
    char buf[4096];
    while (size_t n = fread(buf, 1, sizeof(buf), f))
    {
        bytes.append(buf, n);
    }
    fclose(f);
    // strip UTF-8 BOM
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB &&
        (unsigned char)bytes[2] == 0xBF)
    {
        bytes.erase(0, 3);
    }
    out = Utf8ToWide(bytes);
    // normalize newlines to \n
    std::wstring norm;
    norm.reserve(out.size());
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == L'\r')
        {
            if (i + 1 < out.size() && out[i + 1] == L'\n')
                continue;
            norm.push_back(L'\n');
        } else
        {
            norm.push_back(out[i]);
        }
    }
    out.swap(norm);
    return true;
}

UINT ParseVkToken(const std::wstring& tok)
{
    if (tok.size() == 1)
    {
        const wchar_t c = towupper(tok[0]);
        if (c >= L'A' && c <= L'Z')
            return static_cast<UINT>(c);
        if (c >= L'0' && c <= L'9')
            return static_cast<UINT>(c);
    }
    if (!tok.empty() && (tok[0] == L'F' || tok[0] == L'f'))
    {
        int n = _wtoi(tok.c_str() + 1);
        if (n >= 1 && n <= 24)
            return VK_F1 + (n - 1);
    }
    return 0;
}

} // namespace

bool ParseHotkey(const std::wstring& text, HotkeySpec& out, std::wstring* error)
{
    out = {};
    std::wstring s = Trim(text);
    if (s.empty())
    {
        if (error)
            *error = L"empty hotkey";
        return false;
    }

    UINT mods = 0;
    UINT vk = 0;

    // split on +
    size_t start = 0;
    while (start <= s.size())
    {
        size_t plus = s.find(L'+', start);
        std::wstring part =
            Trim(plus == std::wstring::npos ? s.substr(start) : s.substr(start, plus - start));
        const std::wstring pl = ToLower(part);
        if (pl == L"ctrl" || pl == L"control")
            mods |= MOD_CONTROL;
        else if (pl == L"alt")
            mods |= MOD_ALT;
        else if (pl == L"shift")
            mods |= MOD_SHIFT;
        else if (pl == L"win" || pl == L"windows" || pl == L"meta")
            mods |= MOD_WIN;
        else
        {
            vk = ParseVkToken(part);
            if (!vk)
            {
                if (error)
                    *error = L"unknown hotkey token: " + part;
                return false;
            }
        }
        if (plus == std::wstring::npos)
            break;
        start = plus + 1;
    }

    if (!vk)
    {
        if (error)
            *error = L"hotkey missing key: " + text;
        return false;
    }
    out.modifiers = mods | MOD_NOREPEAT;
    out.vk = vk;
    out.display = FormatHotkeyDisplay(mods, vk);
    return true;
}

std::wstring BuildPromptPayload(const std::wstring& promptTemplate, const std::wstring& editorText,
                                bool fenceEditorText)
{
    // Placeholder expansion
    if (promptTemplate.find(L"{{TEXT}}") != std::wstring::npos ||
        promptTemplate.find(L"{{CONTEXT}}") != std::wstring::npos)
    {
        std::wstring out = promptTemplate;
        auto replaceAll = [](std::wstring& s, const std::wstring& a, const std::wstring& b) {
            size_t pos = 0;
            while ((pos = s.find(a, pos)) != std::wstring::npos)
            {
                s.replace(pos, a.size(), b);
                pos += b.size();
            }
        };
        replaceAll(out, L"{{TEXT}}", editorText);
        // Leave an empty context block the user can fill in the AI box if needed.
        replaceAll(out, L"{{CONTEXT}}", L"");
        return out;
    }

    // Default: instructions + optional fenced body
    std::wstring prompt = Trim(promptTemplate);
    // trim right only more carefully
    while (!prompt.empty() && iswspace(prompt.back()))
        prompt.pop_back();

    if (editorText.empty())
        return prompt;

    if (fenceEditorText)
    {
        std::wstring out = prompt;
        if (out.empty() || out.back() != L':')
            out.push_back(L':');
        out += L"\n\n```\n";
        out += editorText;
        if (!editorText.empty() && editorText.back() != L'\n')
            out.push_back(L'\n');
        out += L"```\n";
        return out;
    }

    std::wstring out = prompt;
    if (!out.empty() && out.back() != L'\n')
        out += L"\n\n";
    out += editorText;
    return out;
}

void GetBuiltinBindings(std::vector<HotkeyBinding>& out)
{
    out.clear();
    const UINT mod = MOD_CONTROL | MOD_ALT;

    auto add = [&](wchar_t key, const wchar_t* name, const wchar_t* label, const wchar_t* service,
                   const wchar_t* url, const wchar_t* hint, const wchar_t* prompt, bool capture,
                   bool image) {
        HotkeyBinding b;
        b.hotkey = HK(mod, static_cast<UINT>(key));
        b.name = name;
        b.templateId = name;
        b.label = label;
        b.service = service;
        b.aiUrl = url;
        b.pageTitleHint = hint;
        b.promptBody = prompt;
        b.captureEditor = capture;
        b.requireClipboardImage = image;
        b.action = ActionKind::SendToAi;
        out.push_back(std::move(b));
    };

    add(L'J', L"grammar_meta", L"Grammar quick (Meta)", L"meta", L"https://www.meta.ai/", L"Meta",
        L"Light edit only. Fix grammar, spelling, and awkward wording.\n"
        L"Keep my voice. Minimal changes.\n",
        true, false);
    add(L'K', L"fact_gemini", L"Deep fact-check (Gemini)", L"gemini",
        L"https://gemini.google.com/app", L"Gemini",
        L"Deep fact-check with sources for claims in the message.\n", true, false);
    add(L'L', L"idea_grok", L"Idea collab (Grok)", L"grok", L"https://grok.com/", L"Grok",
        L"Collaborate on this idea. Push back and suggest next steps.\n", true, false);
    add(L'O', L"grammar_context_meta", L"Grammar in context (Meta)", L"meta",
        L"https://www.meta.ai/", L"Meta",
        L"Polish MY message only. Do not over-explain context readers already see.\n\n"
        L"My message:\n```\n{{TEXT}}\n```\n\nOptional context:\n```\n{{CONTEXT}}\n```\n",
        true, false);
    add(L'I', L"screenshot_meta", L"Screenshot review (Meta)", L"meta", L"https://www.meta.ai/",
        L"Meta",
        L"Screenshot attached. Polish my draft comment; do not over-explain visible context.\n",
        false, true);
}

bool LoadConfigFile(const std::wstring& pathOrEmpty, AppConfig& cfg, std::wstring* error)
{
    cfg.dataDir = GetAppDataDir(true);

    std::wstring path = pathOrEmpty;
    if (path.empty())
    {
        std::wstring seedErr;
        if (!EnsureUserConfigFile(&path, &seedErr))
        {
            // Last-chance: load install template in-place without copying (read-only run).
            const std::wstring tmpl = GetInstallConfigTemplatePath();
            if (FileExists(tmpl))
            {
                path = tmpl;
                QP_LOG_WARN(L"config: could not seed AppData (%s) — reading template %s",
                            seedErr.c_str(), tmpl.c_str());
            } else
            {
                if (error)
                    *error = seedErr.empty() ? L"no config file" : seedErr;
                return false;
            }
        }
    }
    if (!FileExists(path))
    {
        if (error)
            *error = L"config not found: " + path;
        return false;
    }

    // Empty / unreadable user file → try newest backup once.
    {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        {
            ULARGE_INTEGER uli{};
            uli.HighPart = fad.nFileSizeHigh;
            uli.LowPart = fad.nFileSizeLow;
            if (uli.QuadPart == 0)
            {
                const std::wstring backups = GetUserBackupsDir(false);
                // pick newest qiuckprompts-*.ini by name
                WIN32_FIND_DATAW fd{};
                HANDLE h = FindFirstFileW(PathJoin(backups, L"qiuckprompts-*.ini").c_str(), &fd);
                std::wstring best;
                if (h != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                            continue;
                        if (best.empty() || fd.cFileName > best)
                            best = fd.cFileName;
                    } while (FindNextFileW(h, &fd));
                    FindClose(h);
                }
                if (!best.empty())
                {
                    const std::wstring bp = PathJoin(backups, best);
                    QP_LOG_WARN(L"config: user ini empty — restoring backup %s", bp.c_str());
                    BackupFileToUserBackups(path); // keep the empty one too
                    CopyFilePath(bp, path, false);
                }
            }
        }
    }

    cfg.configPath = path;
    {
        const size_t slash = path.find_last_of(L"\\/");
        cfg.configDir = (slash == std::wstring::npos) ? GetExeDir() : path.substr(0, slash);
    }

    std::wstring text;
    if (!ReadUtf8File(path, text, error))
        return false;

    std::wstring section;
    std::map<std::wstring, std::map<std::wstring, std::wstring>> secs;

    // Line parser with multi-line prompt<<< ... >>> blocks.
    // Inside a prompt block: keep ';' and blank lines (full prompt text).
    std::wstringstream ss(text);
    std::wstring line;
    bool inPrompt = false;
    std::wstring promptAccum;
    while (std::getline(ss, line))
    {
        // normalize \r already stripped by ReadUtf8File mostly; still strip trailing \r
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();

        if (inPrompt)
        {
            const std::wstring trimmed = Trim(line);
            if (trimmed == L">>>")
            {
                secs[section][L"prompt"] = promptAccum;
                inPrompt = false;
                promptAccum.clear();
                continue;
            }
            if (!promptAccum.empty())
                promptAccum.push_back(L'\n');
            promptAccum += line; // raw line, comments preserved
            continue;
        }

        // Outside prompt: strip ; comments
        {
            const size_t sc = line.find(L';');
            if (sc != std::wstring::npos)
                line = line.substr(0, sc);
        }
        line = Trim(line);
        if (line.empty())
            continue;

        if (line.front() == L'[' && line.back() == L']')
        {
            section = ToLower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }

        // prompt<<<  or  prompt = <<<
        {
            std::wstring k, v;
            if (SplitKeyValue(line, k, v))
            {
                const std::wstring kl = ToLower(k);
                const std::wstring vl = Trim(v);
                if (kl == L"prompt" && (vl == L"<<<" || vl == L"\"\"\"" || vl == L"'''"))
                {
                    inPrompt = true;
                    promptAccum.clear();
                    continue;
                }
                secs[section][kl] = v;
                continue;
            }
            // bare form: prompt<<<
            if (ToLower(line) == L"prompt<<<" || line == L"prompt<<<")
            {
                inPrompt = true;
                promptAccum.clear();
                continue;
            }
        }
    }
    if (inPrompt)
    {
        QP_LOG_WARN(L"config: unclosed prompt<<< block in section [%s]", section.c_str());
        secs[section][L"prompt"] = promptAccum;
    }

    // settings
    auto sit = secs.find(L"settings");
    if (sit != secs.end())
    {
        const auto& kv = sit->second;
        auto get = [&](const wchar_t* key) -> std::wstring {
            auto it = kv.find(key);
            return it == kv.end() ? L"" : it->second;
        };
        if (!get(L"browser_hint").empty())
            cfg.workflow.browserTitleHint = get(L"browser_hint");
        if (!get(L"log_level").empty())
            cfg.logLevel = Logger::ParseLevel(get(L"log_level"));
        if (!get(L"fence_editor_text").empty())
            cfg.workflow.fenceEditorText = get(L"fence_editor_text") != L"0";
        if (!get(L"default_ai_url").empty())
            cfg.workflow.defaultAiUrl = get(L"default_ai_url");
        if (!get(L"prefer_extension").empty())
            cfg.workflow.preferExtension = get(L"prefer_extension") != L"0";
        if (!get(L"extension_id").empty())
            cfg.extensionId = Trim(get(L"extension_id"));
    }

    cfg.bindings.clear();
    for (const auto& secPair : secs)
    {
        const std::wstring& name = secPair.first;
        if (name.empty() || name == L"settings")
            continue;
        const auto& kv = secPair.second;
        auto get = [&](const wchar_t* key) -> std::wstring {
            auto it = kv.find(key);
            return it == kv.end() ? L"" : it->second;
        };

        HotkeyBinding b;
        b.name = name;
        b.templateId = name;
        b.label = get(L"label").empty() ? name : get(L"label");
        b.service = get(L"service");
        b.aiUrl = get(L"url");
        b.pageTitleHint = get(L"title_hint");
        b.action = ActionKind::SendToAi;

        const std::wstring hk = get(L"hotkey");
        if (hk.empty())
        {
            QP_LOG_WARN(L"config: section [%s] missing hotkey — skip", name.c_str());
            continue;
        }
        std::wstring herr;
        if (!ParseHotkey(hk, b.hotkey, &herr))
        {
            QP_LOG_ERROR(L"config: [%s] bad hotkey '%s': %s", name.c_str(), hk.c_str(),
                         herr.c_str());
            continue;
        }

        b.captureEditor = get(L"capture_editor") != L"0";
        b.requireClipboardImage =
            get(L"require_clipboard_image") == L"1" || get(L"require_clipboard_image") == L"true";
        if (b.requireClipboardImage)
            b.captureEditor = false;

        if (!get(L"fence_editor_text").empty())
            b.fenceEditorText = get(L"fence_editor_text") != L"0";
        else
            b.fenceEditorText = cfg.workflow.fenceEditorText;

        // prompt: inline multi-line / single-line / optional external file
        const std::wstring promptRef = get(L"prompt");
        if (promptRef.empty())
        {
            QP_LOG_WARN(L"config: [%s] missing prompt — skip", name.c_str());
            continue;
        }

        // Prefer treating value as inline text. Only load as file if it looks like a path
        // AND the file exists (keeps old prompt=foo.txt working if someone still uses it).
        bool loadedAsFile = false;
        const bool looksLikePath = promptRef.find(L".txt") != std::wstring::npos ||
                                   promptRef.find(L".md") != std::wstring::npos ||
                                   promptRef.find(L'/') != std::wstring::npos ||
                                   promptRef.find(L'\\') != std::wstring::npos;
        if (looksLikePath)
        {
            std::wstring promptPath = promptRef;
            if (promptPath.find(L':') == std::wstring::npos &&
                !(promptPath.size() >= 2 && (promptPath[0] == L'\\' || promptPath[0] == L'/')))
            {
                promptPath = PathJoin(cfg.configDir, promptRef);
            }
            std::wstring perr;
            if (ReadUtf8File(promptPath, b.promptBody, &perr))
            {
                loadedAsFile = true;
                QP_LOG_DEBUG(L"config: [%s] prompt loaded from file %s", name.c_str(),
                             promptPath.c_str());
            }
        }
        if (!loadedAsFile)
        {
            b.promptBody = promptRef;
            // trim one trailing newline for tidiness
            while (!b.promptBody.empty() &&
                   (b.promptBody.back() == L'\n' || b.promptBody.back() == L'\r'))
            {
                b.promptBody.pop_back();
            }
            if (!b.promptBody.empty())
                b.promptBody.push_back(L'\n');
        }

        if (b.promptBody.empty())
        {
            QP_LOG_WARN(L"config: [%s] empty prompt — skip", name.c_str());
            continue;
        }

        if (b.aiUrl.empty())
            b.aiUrl = cfg.workflow.defaultAiUrl;

        QP_LOG_INFO(
            L"config: binding [%s] %s service=%s url=%s image=%d capture=%d prompt=%zu wchar",
            b.name.c_str(), b.hotkey.display.c_str(), b.service.c_str(), b.aiUrl.c_str(),
            b.requireClipboardImage ? 1 : 0, b.captureEditor ? 1 : 0, b.promptBody.size());
        cfg.bindings.push_back(std::move(b));
    }

    if (cfg.bindings.empty())
    {
        if (error)
            *error = L"config loaded but no valid bindings: " + path;
        return false;
    }
    return true;
}

bool ParseCommandLine(int argc, wchar_t** argv, AppConfig& cfg, std::wstring* error)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg = argv[i] ? argv[i] : L"";
        if (arg.empty())
            continue;

        if (arg == L"--console")
        {
            cfg.console = true;
            continue;
        }
        if (arg == L"--self-test" || arg == L"--help" || arg == L"-h" || arg == L"/?")
            continue;
        if (arg == L"--insert-only")
        {
            cfg.forceInsertOnly = true;
            continue;
        }
        if (arg == L"--replace-running")
        {
            cfg.replaceRunning = true;
            continue;
        }
#if QP_DEV_TOOLS
        if (arg == L"--crash-test")
        {
            cfg.crashTest = true;
            continue;
        }
#endif
        if (arg == L"--no-uia")
        {
            cfg.workflow.pageReadyUseUia = false;
            continue;
        }
        if (arg == L"--no-extension")
        {
            cfg.workflow.preferExtension = false;
            continue;
        }
        if (arg == L"--native-messaging-host")
        {
            // Handled in wWinMain before App::Run; ignore here.
            continue;
        }
        if (arg == L"--hotkey-on-press")
        {
            cfg.hotkeyTrigger = HotkeyTriggerMode::OnPress;
            continue;
        }
        if (arg == L"--hotkey-on-release")
        {
            cfg.hotkeyTrigger = HotkeyTriggerMode::OnRelease;
            continue;
        }

        std::wstring v;
        if (TakeEqValue(i, argc, argv, arg, L"--config", v))
        {
            cfg.configPath = v;
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--log-level", v))
        {
            cfg.logLevel = Logger::ParseLevel(v);
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--log-file", v))
        {
            cfg.logPath = v;
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--paste-delay", v))
        {
            cfg.pasteDelayMs = ClampInt(_wtoi(v.c_str()), 0, 5000);
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--ai-url", v))
        {
            cfg.workflow.defaultAiUrl = v;
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--browser-hint", v))
        {
            cfg.workflow.browserTitleHint = v;
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--page-title-hint", v))
        {
            cfg.workflow.pageTitleHint = v;
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--navigate-delay", v) ||
            TakeEqValue(i, argc, argv, arg, L"--page-ready-timeout", v))
        {
            cfg.workflow.pageReadyTimeoutMs = ClampInt(_wtoi(v.c_str()), 500, 120000);
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--page-ready-min", v))
        {
            cfg.workflow.pageReadyMinMs = ClampInt(_wtoi(v.c_str()), 0, 10000);
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--hotkey-release-timeout", v))
        {
            cfg.hotkeyReleaseTimeoutMs = ClampInt(_wtoi(v.c_str()), 100, 30000);
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--hotkey-release-poll", v))
        {
            cfg.hotkeyReleasePollMs = ClampInt(_wtoi(v.c_str()), 5, 100);
            continue;
        }

        if (arg.size() >= 2 && arg[0] == L'-')
        {
            if (error)
                *error = L"Unknown option: " + arg;
            return false;
        }
    }
    return true;
}

} // namespace qp
