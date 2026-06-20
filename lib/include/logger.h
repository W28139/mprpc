#pragma once
#include <string>     
#include <thread>  
#include "lockqueue.h"
#include "logger.h"
#include <time.h>
#include <iostream>
enum LogLevel
{
    INFO,   // 普通信息
    ERROR,  // 错误信息
};

struct LogMsg {
    LogLevel level;
    std::string msg;
};

// Mprpc提供的日志系统
class Logger
{
public:
    // 获取日志的单例
    static Logger& GetInstance();
    // 设置日志级别
    void SetLogLevel(LogLevel level);
    // 写日志,把日志写入lockqueue缓冲区中
    void Log(LogLevel level, std::string msg);
private:
    int m_loglevel; // 记录入职级别
    LockQueue<LogMsg> m_lockQue; 

    Logger();
    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;
};

// 定义日志级别宏
#define LOG_INFO(logmsgformat, ...) \
    do { \
        char c[1024] = {0}; \
        snprintf(c, 1024, logmsgformat, ##__VA_ARGS__); \
        Logger::GetInstance().Log(INFO, c); \
    } while (0)

#define LOG_ERR(logmsgformat, ...) \
    do { \
        char c[1024] = {0}; \
        snprintf(c, 1024, logmsgformat, ##__VA_ARGS__); \
        Logger::GetInstance().Log(ERROR, c); \
    } while (0)
