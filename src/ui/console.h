// RBS Engine - drop-down developer console (toggle with ` or F1).
#pragma once

#include <string>
#include <vector>

namespace rbs {

class Console {
public:
    bool open = false;
    void Toggle() { open = !open; }
    // returns true while the console captures keyboard input
    bool Update();
    void Draw(int w, int h);

private:
    std::string m_input;
    std::vector<std::string> m_history;
    int m_historyPos = -1;
    int m_scroll = 0;
    float m_anim = 0;
};

}  // namespace rbs
