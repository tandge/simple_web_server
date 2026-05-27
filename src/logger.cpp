#include "logger.h"

#include <cstdarg>
#include <ctime>
#include <cstring>

Logger::Level Logger::s_level = Logger::LINFO;
FILE* Logger::s_output = nullptr;

void Logger::setLevel(Level level) {
    s_level = level;
}

void Logger::setOutputFile(const std::string& path) {
    if (s_output && s_output != stdout) {
        std::fclose(s_output);
    }
    if (path.empty()) {
        s_output = stdout;
    } else {
        s_output = std::fopen(path.c_str(), "a");
        if (!s_output) {
            s_output = stdout;
        }
    }
}

void Logger::flush() {
    if (s_output) std::fflush(s_output);
}

static const char* levelName(Logger::Level lv) {
    switch (lv) {
        case Logger::LDEBUG: return "DEBUG";
        case Logger::LINFO:  return "INFO";
        case Logger::LWARN:  return "WARN";
        case Logger::LERROR: return "ERROR";
    }
    return "UNK";
}

void Logger::log(Level level, const char* file, int line,
                 const char* fmt, ...) {
    if (level < s_level) return;

    if (!s_output) s_output = stdout;

    // 时间戳
    std::time_t now = std::time(nullptr);
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S",
                  std::localtime(&now));

    // 基本格式
    std::fprintf(s_output, "[%s] [%s] %s:%d - ",
                 time_buf, levelName(level), file, line);

    // 用户内容
    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(s_output, fmt, args);
    va_end(args);

    std::fprintf(s_output, "\n");
    flush();
}
