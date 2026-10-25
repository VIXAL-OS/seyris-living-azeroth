// A key watched for presses (2026-10-25: the lantern key, the compare key), the way the snapshot key is read
// (debug/Snapshot.cpp): the window's messages as the client gets them (OnInput, never swallowed: a binding of the
// client's on the same key still runs) and a poll while the client's window has the focus (some input paths don't reach
// the subclassed window; the core polls F9 so). Both paths share the key's last state: one press, one action.
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace wxl_livingazeroth::keys
{
    /// The client's window has the focus (the foreground window belongs to this process).
    inline bool ClientFocused()
    {
        const HWND w = GetForegroundWindow();
        DWORD pid = 0;
        if (w) GetWindowThreadProcessId(w, &pid);
        return w && pid == GetCurrentProcessId();
    }

    /// A Windows virtual-key code as the panels name it: "F7", the key's own name, or "key code N"; `none` for 0.
    inline std::string KeyName(int key, const char* none = "no key")
    {
        if (key <= 0 || key >= 256) return none;
        if (key >= VK_F1 && key <= VK_F24) return "F" + std::to_string(key - VK_F1 + 1);
        char name[64] = "";
        const UINT scan = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
        if (scan && GetKeyNameTextA(static_cast<LONG>(scan << 16), name, sizeof(name)) > 0) return name;
        char code[32];
        std::snprintf(code, sizeof(code), "key code %d", key);
        return code;
    }

    struct KeyWatch
    {
        bool down = false;     // the key's last state, as either path saw it
        bool pending = false;  // pressed since the last Take

        void Observe(bool isDown)
        {
            if (isDown && !down) pending = true;
            down = isDown;
        }
        /// From the OnInput subscriber: one window message.
        void Message(int key, uint32_t message, uintptr_t wparam)
        {
            if (key <= 0 || key >= 256 || static_cast<int>(wparam) != key) return;
            if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) Observe(true);
            else if (message == WM_KEYUP || message == WM_SYSKEYUP) Observe(false);
        }
        /// Once a frame.
        void Poll(int key)
        {
            if (key > 0 && key < 256) Observe(ClientFocused() && (GetAsyncKeyState(key) & 0x8000) != 0);
        }
        /// Whether it was pressed since the last call.
        bool Take()
        {
            const bool p = pending;
            pending = false;
            return p;
        }
    };
}
