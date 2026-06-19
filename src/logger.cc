#include"logger.h"

Logger& Logger::GetInstance() {
    static Logger logger;
    return logger;
}

Logger::Logger() {
    // 启动专门的日志线程
    std::thread writeLogTask([&]() {
        for (;;) {
            // 1. 从队列取出日志消息
            LogMsg logmsg = m_lockQue.Pop();

            // 2. 获取当前日期和时间
            time_t now = time(nullptr);
            tm *nowtm = localtime(&now);

            // 制作文件名：格式为 2026-06-19-log.txt
            char file_name[128];
            sprintf(file_name, "%d-%d-%d-log.txt", nowtm->tm_year + 1900, nowtm->tm_mon + 1, nowtm->tm_mday);

            FILE *pf = fopen(file_name, "a+");
            if (pf == nullptr) {
                std::cout << "logger file : " << file_name << " open error!" << std::endl;
                continue;
            }

            // 制作时间戳：格式为 16:30:05 => 
            char time_buf[128];
            sprintf(time_buf, "%02d:%02d:%02d => ", nowtm->tm_hour, nowtm->tm_min, nowtm->tm_sec);

            // 获取级别标签
            std::string level_tag = (logmsg.level == INFO ? "[INFO] " : "[ERROR] ");

            // 3. 写入文件：时间戳 + 级别 + 消息
            fputs(time_buf, pf);
            fputs(level_tag.c_str(), pf);
            fputs(logmsg.msg.c_str(), pf);
            fputs("\n", pf);

            fclose(pf);
        }
    });
    // 设置分离线程
    writeLogTask.detach();
}

void Logger::SetLogLevel(LogLevel level) 
{
    m_loglevel = level;
}

void Logger::Log(LogLevel level, std::string msg) 
{
    m_lockQue.Push({level, msg});
}