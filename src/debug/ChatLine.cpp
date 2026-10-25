#include "ChatLine.hpp"

#include "game/Script.hpp"

#include <windows.h>

namespace wxl_livingazeroth::chat
{
    namespace
    {
        void (*g_capture)(const std::string&) = nullptr;

        // The client's Lua, guarded: a script context torn down mid-call (a reload) fails cleanly.
        bool Run(const char* lua)
        {
            __try
            {
                if (!wxl::game::script::Context()) return false;
                wxl::game::script::Execute(lua, "LivingAzerothChat");
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
    }

    void Say(const std::string& text)
    {
        const std::string line = "Living Azeroth: " + text;
        if (g_capture) { g_capture(line); return; }
        std::string lua = "if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage(\"";
        for (const char ch : line)
            if (ch == '\\') lua += "\\\\";
            else if (ch == '"') lua += "\\\"";
            else if (ch == '\n' || ch == '\r') lua += ' ';
            else lua += ch;
        lua += "\", 0.55, 0.8, 1.0) end";
        Run(lua.c_str());
    }

    void SetCaptureForTests(void (*capture)(const std::string& line)) { g_capture = capture; }
}
