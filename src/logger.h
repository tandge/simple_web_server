#pragma once
/**
 * 简单日志模块
 * 支持多级别输出，可写文件或控制台
 */

#include <cstdio>
#include <string>

class Logger {
public:
    enum Level { LDEBUG, LINFO, LWARN, LERROR };

    /** 设置日志级别，低于此级别的不输出 */
    static void setLevel(Level level);

    /** 设置输出文件，为空则输出到stdout */
    static void setOutputFile(const std::string& path);

    /** 核心日志函数 */
    static void log(Level level, const char* file, int line,
                    const char* fmt, ...);

private:
    static Level s_level;
    static FILE* s_output;
    static void flush();
};

// 使用宏简化调用，自动填入文件名和行号
#define LOG_DEBUG(...) Logger::log(Logger::LDEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  Logger::log(Logger::LINFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  Logger::log(Logger::LWARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Logger::log(Logger::LERROR, __FILE__, __LINE__, __VA_ARGS__)
