#ifndef LOG_H
#define LOG_H

// 日志级别
#define LOG_LEVEL_INFO   0
#define LOG_LEVEL_WARNING 1
#define LOG_LEVEL_ERROR  2

// 设置日志级别（小于该级别的日志不输出到文件）
void log_set_level(int level);

// 日志函数
void log_info(const char *fmt, ...);
void log_warning(const char *fmt, ...);
void log_error(const char *fmt, ...);

// 关闭日志系统（可选）
void log_close(void);

#endif