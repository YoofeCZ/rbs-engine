#include "core/log.h"
#include "core/common.h"

#include <cstdarg>
#include <cstdio>

namespace rbs {

static std::vector<LogLine> g_lines;

static void RaylibLogCallback(int level, const char* text, va_list args) {
    if (level < LOG_WARNING) return;  // keep raylib's INFO spam out of the console
    char buf[1024];
    vsnprintf(buf, sizeof(buf), text, args);
    LogPrint(level >= LOG_ERROR ? Color{255, 90, 90, 255} : YELLOW, "[raylib] %s", buf);
}

void LogInit() { SetTraceLogCallback(RaylibLogCallback); }

void LogPrint(Color color, const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("%s\n", buf);
    fflush(stdout);
    g_lines.push_back({buf, color, GetTime()});
    if (g_lines.size() > 512) g_lines.erase(g_lines.begin(), g_lines.begin() + 64);
}

const std::vector<LogLine>& LogLines() { return g_lines; }

std::string StrFormat(const char* fmt, ...) {
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

std::string ToLower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::vector<std::string> SplitWords(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : s) {
        if (c == '"') { quoted = !quoted; continue; }
        if (!quoted && (c == ' ' || c == '\t')) {
            if (!cur.empty()) out.push_back(cur), cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool StartsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool EndsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

}  // namespace rbs
