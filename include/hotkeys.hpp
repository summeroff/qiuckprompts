#pragma once

#include <windows.h>

#include "config.hpp"

#include <functional>
#include <vector>

namespace qp {

class HotkeyManager {
public:
    using Callback = std::function<void(int id, const HotkeyBinding& binding)>;

    HotkeyManager() = default;
    ~HotkeyManager();

    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    void SetCallback(Callback cb) { callback_ = std::move(cb); }

    // Assigns binding.id = 1..N and registers. Partial success OK.
    bool RegisterAll(HWND hwnd, std::vector<HotkeyBinding> bindings,
                     std::wstring* error = nullptr);

    void UnregisterAll();
    void OnWmHotkey(WPARAM wParam);
    const HotkeyBinding* FindById(int id) const;

    size_t RegisteredCount() const { return registered_.size(); }
    const std::vector<HotkeyBinding>& Bindings() const { return registered_; }

private:
    HWND hwnd_ = nullptr;
    std::vector<HotkeyBinding> registered_;
    Callback callback_;
};

} // namespace qp
