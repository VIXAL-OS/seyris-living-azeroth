// One line in the chat frame (2026-10-25: the lantern and compare keys say what they did), as the snapshot announces its
// file: DEFAULT_CHAT_FRAME:AddMessage through the client's Lua, main thread only. Nothing reaches addons' code.
#pragma once

#include <string>

namespace wxl_livingazeroth::chat
{
    /// "Living Azeroth: <text>" in the chat frame (light blue), when the client has a script context; else nothing.
    void Say(const std::string& text);

    /// Tests: lines go to `capture` instead of the client (null: back to the client).
    void SetCaptureForTests(void (*capture)(const std::string& line));
}
