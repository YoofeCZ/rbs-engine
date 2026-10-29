// RBS Engine - logging (stdout + in-game console buffer).
#pragma once

#include <string>
#include <vector>
#include <raylib.h>

namespace rbs {

struct LogLine {
    std::string text;
    Color color;
    double time;
};

void LogInit();
void LogPrint(Color color, const char* fmt, ...);
const std::vector<LogLine>& LogLines();

#define LOG_INFO(...) ::rbs::LogPrint(RAYWHITE, __VA_ARGS__)
#define LOG_WARN(...) ::rbs::LogPrint(YELLOW, __VA_ARGS__)
#define LOG_ERR(...) ::rbs::LogPrint(Color{255, 90, 90, 255}, __VA_ARGS__)

}  // namespace rbs
