#include "ui/console.h"
#include "core/common.h"
#include "core/cvar.h"
#include "core/log.h"

namespace rbs {

bool Console::Update() {
    float target = open ? 1.0f : 0.0f;
    m_anim += (target - m_anim) * fminf(1.0f, GetFrameTime() * 14.0f);
    if (!open) return false;
    int c;
    while ((c = GetCharPressed()) != 0) {
        if (c == '`' || c == '~') continue;
        if (c >= 32 && c < 127) m_input += (char)c;
    }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !m_input.empty()) m_input.pop_back();
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        if (!m_input.empty()) {
            LOG_INFO("] %s", m_input.c_str());
            m_history.push_back(m_input);
            cvars::Execute(m_input);
            m_input.clear();
        }
        m_historyPos = -1;
        m_scroll = 0;
    }
    if (IsKeyPressed(KEY_UP) && !m_history.empty()) {
        m_historyPos = m_historyPos < 0 ? (int)m_history.size() - 1 : std::max(0, m_historyPos - 1);
        m_input = m_history[m_historyPos];
    }
    if (IsKeyPressed(KEY_DOWN) && m_historyPos >= 0) {
        m_historyPos++;
        if (m_historyPos >= (int)m_history.size()) {
            m_historyPos = -1;
            m_input.clear();
        } else {
            m_input = m_history[m_historyPos];
        }
    }
    if (IsKeyPressed(KEY_TAB) && !m_input.empty()) {
        auto matches = cvars::Complete(m_input);
        if (matches.size() == 1) m_input = matches[0] + " ";
        else if (matches.size() > 1) {
            std::string line;
            for (auto& m : matches) line += m + "  ";
            LOG_INFO("%s", line.c_str());
        }
    }
    if (IsKeyPressed(KEY_PAGE_UP)) m_scroll += 10;
    if (IsKeyPressed(KEY_PAGE_DOWN)) m_scroll = std::max(0, m_scroll - 10);
    float wheel = GetMouseWheelMove();
    if (wheel != 0) m_scroll = std::max(0, m_scroll + (int)(wheel * 3));
    return true;
}

void Console::Draw(int w, int h) {
    if (m_anim < 0.01f) return;
    int ch = (int)(h * 0.5f * m_anim);
    DrawRectangle(0, 0, w, ch, Color{12, 10, 10, 225});
    DrawRectangle(0, ch - 2, w, 2, Color{170, 110, 50, 255});
    const auto& lines = LogLines();
    int lineH = 12;
    int y = ch - 30;
    int idx = (int)lines.size() - 1 - m_scroll;
    for (; idx >= 0 && y > -lineH; --idx, y -= lineH) DrawText(lines[idx].text.c_str(), 8, y, 10, lines[idx].color);
    std::string prompt = "] " + m_input + ((int)(GetTime() * 2) % 2 ? "_" : "");
    DrawText(prompt.c_str(), 8, ch - 16, 10, Color{255, 200, 120, 255});
    DrawText("RBS ENGINE " RBS_VERSION, w - 110, ch - 16, 10, Color{120, 100, 90, 255});
}

}  // namespace rbs
