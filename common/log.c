#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>// 可变参数支持
#include <time.h>
#include <pthread.h>

// 宏定义:日志输出的文件名,所有日志都会追加写入这个文件
#define LOG_FILE "server.log"

// ===================== 全局静态变量 =====================
// 静态变量:只能在当前log.c文件中访问,外部无法直接修改,保证封装性

// 当前日志级别:默认是INFO级别,低于该级别的日志不会输出
static int log_level = LOG_LEVEL_INFO;

// 日志文件指针:NULL表示未打开文件
static FILE *log_fp = NULL;

// 日志互斥锁:保证多线程同时写日志时,不会出现日志乱序、覆盖
// PTHREAD_MUTEX_INITIALIZER是静态初始化方式,无需手动init
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// ===================== 内部工具函数 =====================
// 静态函数:只能在log.c内部调用,对外隐藏实现细节

// 功能:打开日志文件(惰性打开:第一次写日志时才打开,不提前占用)
static void log_open(void) {
    // 如果文件还没打开,才执行打开操作
    if (log_fp == NULL) {
        // 以"追加模式"打开日志文件
        // a:append,不会覆盖原有内容,每次写入都在末尾
        log_fp = fopen(LOG_FILE, "a");
        // 如果打开失败,打印错误信息
        if (!log_fp) {
            perror("fopen log file");
        }
    }
}

// ===================== 对外接口函数 =====================
// 功能:设置全局日志输出级别
// 例如:设置为WARNING,则只有WARNING和ERROR会输出
void log_set_level(int level) {
    log_level = level;
}

// 功能:关闭日志文件,释放资源
// 服务端优雅退出时必须调用,确保日志全部写入磁盘
void log_close(void) {
    if (log_fp) {
        fclose(log_fp);  // 关闭文件
        log_fp = NULL;   // 置空,防止野指针
    }
}

// ===================== 核心日志写入函数 =====================
// 功能:真正执行日志写入的底层函数
// 参数:
//   level_str: 日志级别字符串,如"INFO"、"WARNING"
//   fmt: 格式化字符串
//   args: 可变参数列表
static void log_write(const char *level_str, const char *fmt, va_list args) {
    // 第一步:确保日志文件已经打开
    log_open();
    // 如果文件打开失败,直接返回,不继续执行
    if (!log_fp) return;

    // ===================== 多线程安全保护 =====================
    // 加锁:保证同一时间只有一个线程能执行下面的写操作
    // 防止多线程同时写日志导致日志内容错乱、交叉
    pthread_mutex_lock(&log_mutex);

    // ===================== 拼接时间戳 =====================
    // 获取当前系统时间(从1970年开始的秒数)
    time_t t = time(NULL);
    // 将时间戳转换为本地时间的年/月/日/时/分/秒
    struct tm *tm = localtime(&t);

    // 把【时间 + 级别】写入日志文件
    fprintf(log_fp, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
            tm->tm_year + 1900,  // tm_year从1900年开始算,所以+1900
            tm->tm_mon + 1,      // tm_mon范围0-11,所以+1变成1-12月
            tm->tm_mday,         // 日
            tm->tm_hour,         // 时
            tm->tm_min,          // 分
            tm->tm_sec,          // 秒
            level_str);          // 日志级别:INFO/WARNING/ERROR

    // ===================== 写入用户日志内容 =====================
    // vfprintf:专门用于处理可变参数va_list的格式化输出
    // 把用户传入的fmt和args格式化后写入文件
    vfprintf(log_fp, fmt, args);

    // 换行:每条日志占一行
    fputc('\n', log_fp);

    // 立即刷新缓冲区:确保日志立刻写入磁盘,不留在内存中
    // 防止程序突然崩溃,日志丢失
    fflush(log_fp);

    // 解锁:允许其他线程写日志
    pthread_mutex_unlock(&log_mutex);
}

// ===================== 对外简易接口 =====================
// 功能:输出INFO级别日志(普通信息)
void log_info(const char *fmt, ...) {
    // 日志级别过滤:如果当前设置的级别高于INFO,则不输出
    if (log_level > LOG_LEVEL_INFO) return;

    // 定义可变参数列表
    va_list args;
    // 初始化可变参数:从fmt之后开始取参数
    va_start(args, fmt);
    // 调用底层写日志函数
    log_write("INFO", fmt, args);
    // 结束可变参数处理
    va_end(args);
}

// 功能:输出WARNING级别日志(警告)
void log_warning(const char *fmt, ...) {
    if (log_level > LOG_LEVEL_WARNING) return;
    va_list args;
    va_start(args, fmt);
    log_write("WARNING", fmt, args);
    va_end(args);
}

// 功能:输出ERROR级别日志(错误)
void log_error(const char *fmt, ...) {
    if (log_level > LOG_LEVEL_ERROR) return;
    va_list args;
    va_start(args, fmt);
    log_write("ERROR", fmt, args);
    va_end(args);
}