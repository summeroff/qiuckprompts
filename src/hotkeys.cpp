#include "hotkeys.hpp"
#include "logger.hpp"
#include "util.hpp"

namespace qp {

HotkeyManager::~HotkeyManager() {
    UnregisterAll();
}

void HotkeyManager::UnregisterAll() {
    if (!hwnd_) {
        registered_.clear();
        return;
    }
    for (const auto& b : registered_) {
        if (b.id != 0) {
            if (!UnregisterHotKey(hwnd_, b.id)) {
                QP_LOG_WARN(L"UnregisterHotKey id=%d failed: %s",
                            b.id, LastErrorMessage().c_str());
            } else {
                QP_LOG_TRACE(L"UnregisterHotKey id=%d (%s) ok",
                             b.id, b.hotkey.display.c_str());
            }
        }
    }
    registered_.clear();
}

bool HotkeyManager::RegisterAll(HWND hwnd, std::vector<HotkeyBinding> bindings,
                                std::wstring* error) {
    UnregisterAll();
    hwnd_ = hwnd;

    if (!hwnd_) {
        if (error) *error = L"null hwnd";
        return false;
    }

    int nextId = 1;
    std::vector<std::wstring> failures;

    for (auto& b : bindings) {
        b.id = nextId++;
        const BOOL ok = RegisterHotKey(hwnd_, b.id, b.hotkey.modifiers, b.hotkey.vk);
        if (!ok) {
            const std::wstring msg =
                b.hotkey.display + L" -> " + b.templateId + L"  (" + LastErrorMessage() + L")";
            QP_LOG_ERROR(L"RegisterHotKey FAILED: %s", msg.c_str());
            failures.push_back(msg);
            b.id = 0;
            continue;
        }
        QP_LOG_INFO(L"hotkey registered id=%d %s -> %s (%s)",
                    b.id,
                    b.hotkey.display.c_str(),
                    b.templateId.c_str(),
                    b.label.c_str());
        registered_.push_back(std::move(b));
    }

    if (registered_.empty()) {
        if (error) {
            *error = L"no hotkeys registered";
            if (!failures.empty()) {
                *error += L": ";
                *error += failures.front();
            }
        }
        return false;
    }

    if (!failures.empty()) {
        QP_LOG_WARN(L"%zu hotkey(s) failed to register, %zu ok",
                    failures.size(), registered_.size());
    }
    return true;
}

void HotkeyManager::OnWmHotkey(WPARAM wParam) {
    const int id = static_cast<int>(wParam);
    const HotkeyBinding* b = FindById(id);
    if (!b) {
        QP_LOG_WARN(L"WM_HOTKEY unknown id=%d", id);
        return;
    }
    QP_LOG_DEBUG(L"WM_HOTKEY id=%d %s -> %s",
                 id, b->hotkey.display.c_str(), b->templateId.c_str());
    if (callback_) {
        callback_(id, *b);
    }
}

const HotkeyBinding* HotkeyManager::FindById(int id) const {
    for (const auto& b : registered_) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

} // namespace qp
