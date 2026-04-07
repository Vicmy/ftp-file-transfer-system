#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include <inttypes.h>
#include<sys/stat.h>
#include<pthread.h>
#include<sys/file.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>

#include "../common/utils.h"
#include "../common/protocol.h"
#include "../common/log.h"

#define PORT 8888
// 客户端接收超时：防止客户端卡死导致线程一直阻塞
#define RECV_TIMEOUT_SEC 5   // MODIFIED: recv 超时时间（秒），用于定期检查 running 标志

// ===================== 全局变量 =====================
// 服务运行标志：用于优雅退出（Ctrl+C 时安全关闭）
static volatile int running = 1;  
// 服务端监听 socket（全局，方便信号处理函数关闭）        
static int server_fd = -1;         
// 线程互斥锁：保护活跃线程计数       
static pthread_mutex_t thread_count_mutex = PTHREAD_MUTEX_INITIALIZER;
// 当前正在处理客户端的活跃线程数
static int active_threads = 0;           

// ===================== 线程参数结构体 =====================
// 传递给子线程的客户端信息：socket、IP、端口
typedef struct{
    int client_fd;              //客户端socket
    char ip[INET_ADDRSTRLEN];   //客户端ip
    int port;                   //客户端端口
}client_info_t;

// ===================== 创建服务端存储目录 =====================
// 功能：确保 ./server_files 目录存在，用于存放上传的文件
static void ensure_server_dir(){
    // 创建目录，权限 0755；已存在则不报错
    if (mkdir("./server_files", 0755) != 0 && errno != EEXIST) {
        perror("mkdir");
        // 目录创建失败不影响程序继续，但可能后续文件操作失败
        log_error("Failed to create server_files directory: %s", strerror(errno));

    }
}

// ===================== SIGINT 信号处理函数（Ctrl+C 优雅退出） =====================
// 功能：捕获 Ctrl+C，设置退出标志，并关闭监听 socket
static void sigint_handler(int sig) {
    (void)sig;                                      // 未使用参数，消除编译警告
    log_info("Received SIGINT, shutting down...");
    running = 0;                                    // 设置退出标志，让所有循环安全退出
    
    // 关闭监听 socket，使 accept 立即返回
    if (server_fd != -1) {//有效才关闭
        close(server_fd);
        server_fd = -1;
    }
}

// ===================== 设置 socket 接收超时 =====================
// 功能：防止客户端一直不发数据，导致线程永久阻塞
static void set_recv_timeout(int fd, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;        // 超时秒数
    tv.tv_usec = 0;             //微秒，0就是没有，只按照秒
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// ===================== 客户端处理线程函数 =====================
// 功能：单独处理一个客户端的所有请求（上传/下载/查询偏移）
static void *handle_client(void *arg){
    //解析传入的客户端信息结构体，pthread_create的参数只能是void，它失忆了原来的类型。
    client_info_t *info=(client_info_t *)arg;
    int client_fd=info->client_fd;

    // 打印客户端连接信息
    printf("Client %s:%d connected\n",info->ip,info->port);
    log_info("Client %s:%d connected", info->ip, info->port);
    
    // 设置接收超时，使 recv 能定期返回检查 running
    set_recv_timeout(client_fd, RECV_TIMEOUT_SEC);

    int client_alive=1;// 客户端连接是否保持活跃

    // 循环处理客户端命令（支持一次连接处理多个命令，本项目只处理一个）
    while(client_alive && running){
        // -------------------- 1. 接收客户端命令码（1字节） --------------------
        uint8_t cmd;

        // 接收失败处理
        int n=recv_n(client_fd,&cmd,1);
        if(n != 1){
            // recv 超时或出错，检查 running 标志后继续循环
            if (!running) {
                break;   // 收到退出信号，立即退出循环
            }
            if (n == 0) {
                // 对方关闭连接
                break;
            }
            // 超时或其他错误，继续循环（等待更多数据）
            continue;  
        }

        // -------------------- 2. 根据命令类型处理 --------------------
        switch (cmd){
             // ============= 命令：查询文件偏移（断点续传用） =============
            case CMD_QUERY_OFFSET:{
                // 接收文件名长度
                uint8_t name_len;
                if (recv_n(client_fd, &name_len, 1) != 1) {
                    log_warning("Failed to receive name length from %s:%d", info->ip, info->port);

                    client_alive = 0;
                    break;
                }

                // 接收文件名
                char filename[256];
                if (recv_n(client_fd, filename, name_len) != name_len) {
                    log_warning("Failed to receive filename from %s:%d", info->ip, info->port);

                    client_alive = 0;
                    break;
                }
                filename[name_len] = '\0';// 确保字符串结束

                // 安全检查：防止路径穿越攻击
                if(!is_safe_filename(filename)){
                    log_warning("Unsafe filename from %s:%d: %s", info->ip, info->port, filename);
                    client_alive=0;
                    break;
                }

                // 拼接文件完整路径
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "./server_files/%s", filename);
                
                // 获取文件大小（不存在则为 0）
                uint64_t size = 0;
                FILE *fp = fopen(filepath, "rb");
                 if (fp) {
                    if (fseeko(fp, 0, SEEK_END) != 0) {
                        perror("fseeko");
                        log_error("fseeko failed for %s", filename);

                    } else {
                        size = ftello(fp);
                    }
                    fclose(fp);
                }
               
                // 把服务端已有的文件大小发回客户端
                if(send_n(client_fd, &size, 8)!=8){
                    perror("send_n size");
                    client_alive=0;
                }
                break;
            }

            // ============= 命令：上传文件 =============
            case CMD_UPLOAD:{
                // 接收文件名长度
                uint8_t name_len;
                if(recv_n(client_fd,&name_len,1)!=1){
                    log_warning("Failed to receive name length from %s:%d", info->ip, info->port);
                    client_alive = 0;
                    break;

                }
                
                // 接收文件名
                char filename[256];
                if(recv_n(client_fd,filename,name_len)!=name_len){
                    log_warning("Failed to receive filename from %s:%d", info->ip, info->port);
                    client_alive = 0;
                    break;
                }
                filename[name_len]='\0';

                // 安全检查文件名
                if(!is_safe_filename(filename)){
                    log_warning("Unsafe filename from %s:%d: %s", info->ip, info->port, filename);
                    client_alive=0;
                    break;
                }
            
                // 接收：文件总大小 
                uint64_t file_size,offset;
                if(recv_n(client_fd,&file_size,8)!=8){
                    log_warning("Failed to receive file size/offset from %s:%d", info->ip, info->port);
                    client_alive = 0;
                    break;
                }

                //接收起始偏移量
                if(recv_n(client_fd,&offset,8)!=8){
                    log_warning("Failed to receive file size/offset from %s:%d", info->ip, info->port);

                    client_alive = 0;
                    break;
                }
               
                printf("Upload: %s, size=%" PRIu64 ", offset=%" PRIu64 "\n",
                       filename, file_size, offset);
                log_info("Upload from %s:%d: %s, size=%" PRIu64 ", offset=%" PRIu64,
                         info->ip, info->port, filename, file_size, offset);
                
                // 拼接文件路径
                char filepath[512];
                snprintf(filepath,sizeof(filepath),"./server_files/%s",filename);
                
                // 打开文件：存在则续写，不存在则创建
                FILE *fp=fopen(filepath,"r+b");
                if(!fp){
                    fp=fopen(filepath,"wb");//不存在创建
                }
                if (!fp) {
                    perror("fopen");//再次判断
                    log_error("Failed to open file %s for upload", filename);
                    client_alive = 0;
                    break;
                }
                
                // 加文件锁：防止多线程同时写同一个文件
                int fd = fileno(fp);
                if (flock(fd, LOCK_EX) != 0) {
                    perror("flock");
                    log_error("flock failed for %s", filename);
                    fclose(fp);
                    client_alive = 0;
                    break;
                }

                // 定位到断点续传位置
                if (fseeko(fp, (off_t)offset, SEEK_SET) != 0) {
                    perror("fseeko");
                    log_error("fseeko failed for %s at offset %" PRIu64, filename, offset);
                    flock(fd, LOCK_UN);
                    fclose(fp);
                    client_alive = 0;
                    break;
                }
                
                // ===================== 循环接收客户端数据 =====================
                uint64_t received=offset;
                char buffer[DATA_BLOCK_SIZE];//4096。一次存4kb
                int error = 0;

                while(received<file_size){
                    int to_read = DATA_BLOCK_SIZE;
                    if (file_size - received < DATA_BLOCK_SIZE) {
                        to_read = file_size - received;
                    }

                    // 接收一段数据
                    int n = recv_n(client_fd, buffer, to_read);
                    if (n != to_read) {
                        // 如果连接关闭且已接收完整，则视为成功
                        if (n == 0 && received == file_size) {
                            break;
                        }
                        error = 1;
                        break;
                    }

                    // 写入文件
                    size_t written = fwrite(buffer, 1, n, fp);
                    if (written != n) {
                        perror("fwrite");
                        error = 1;
                        break;
                    }
                    received += n;
                }

                // 解锁 + 关闭文件
                flock(fd, LOCK_UN);
                fclose(fp);

                if (error) {
                    printf("Upload failed\n");
                    log_error("Upload from %s:%d failed", info->ip, info->port);

                } else {
                    printf("Upload finished, received %" PRIu64 " bytes\n", received);
                    log_info("Upload from %s:%d finished, received %" PRIu64 " bytes",
                             info->ip, info->port, received);

                }
                client_alive = 0;
                break;
            }
            
            // ============= 命令：下载文件 =============
            case CMD_DOWNLOAD:{
                // 接收文件名长度
                uint8_t name_len;
                if(recv_n(client_fd,&name_len,1)!= 1){
                    printf("Falied to receive name length\n");
                    client_alive = 0;
                    break;

                }
                
                // 接收文件名
                char filename[256];
                if(recv_n(client_fd,filename,name_len)!= name_len){
                    log_warning("Failed to receive name length from %s:%d", info->ip, info->port);
                    client_alive = 0;
                    break;
                }
                filename[name_len]='\0';

                // 安全检查
                if(!is_safe_filename(filename)){
                    log_warning("Failed to receive filename from %s:%d", info->ip, info->port);
                    client_alive=0;
                    break;
                }
                
                // 接收客户端本地已下载偏移（断点续传）
                uint64_t offset;
                if(recv_n(client_fd,&offset,8)!= 8){
                    log_warning("Failed to receive offset from %s:%d", info->ip, info->port);
                    client_alive = 0;
                    break;

                }

                printf("Download: %s, offset=%" PRIu64 "\n", filename, offset);
                log_info("Download from %s:%d: %s, offset=%" PRIu64,
                         info->ip, info->port, filename, offset);

                
                //拼接文件路径
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "./server_files/%s", filename);
                
                // 打开文件
                FILE *fp = fopen(filepath, "rb");//只读
                if (!fp) {
                    perror("fopen");
                    log_error("File not found: %s", filename);

                    // 发送错误状态给客户端
                    uint8_t status = 0x01;      // 错误状态
                    uint8_t err_code = ERR_FILE_NOT_FOUND;
                    send_n(client_fd, &status, 1);
                    send_n(client_fd, &err_code, 1);
                    client_alive = 0;
                    break;
                }
                
                // 获取文件总大小
                if (fseeko(fp, 0, SEEK_END) != 0) {//跳到文件末尾
                    perror("fseeko");
                    log_error("fseeko failed for %s", filename);
                    uint8_t status = 0x01;
                    uint8_t err_code = ERR_IO;
                    send_n(client_fd, &status, 1);
                    send_n(client_fd, &err_code, 1);
                    fclose(fp);
                    client_alive = 0;
                    break;
                }
                
                uint64_t file_size = ftello(fp);//获取总大小

                // 跳转到续传点
                if (fseeko(fp, (off_t)offset, SEEK_SET) != 0) {
                    perror("fseeko");
                    log_error("fseeko failed for %s at offset %" PRIu64, filename, offset);
                    fclose(fp);
                    uint8_t status = 0x01;
                    uint8_t err_code = ERR_IO;
                    send_n(client_fd, &status, 1);
                    send_n(client_fd, &err_code, 1);
                    client_alive = 0;
                    break;
                }
                
                
                // 发送 成功状态 + 文件总大小
                uint8_t status = 0x00;   // 成功
                if (send_n(client_fd, &status, 1) != 1 ||
                    send_n(client_fd, &file_size, 8) != 8) {
                    perror("send download header");
                    log_error("Failed to send download header for %s", filename);
                    fclose(fp);
                    client_alive = 0;
                    break;
                }
               
                // ===================== 循环发送文件数据 =====================
                char buffer[DATA_BLOCK_SIZE];
                size_t bytes;
                int error = 0;

                while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                    if (send_n(client_fd, buffer, bytes) != bytes) {
                        if (errno == EINTR) {
                            continue;   // 信号中断，重试
                        }
                        perror("send data");
                        log_error("Failed to send data for %s", filename);
                        error = 1;
                        break;
                    }
                }

                fclose(fp);

                if (error) {
                    printf("Download send failed\n");
                    log_error("Download from %s:%d failed", info->ip, info->port);
                } else {
                    printf("Download finished, sent %" PRIu64 " bytes\n", file_size);
                    log_info("Download from %s:%d finished, sent %" PRIu64 " bytes",
                             info->ip, info->port, file_size);
                    
                }
                client_alive = 0;
                break;
            }
                   
            // ============= 未知命令 =====================
            default:
                printf("Unknown commond:%d\n",cmd);
                log_warning("Unknown command %d from %s:%d", cmd, info->ip, info->port);
                client_alive = 0;
                break;
        }
    }
    
    // ===================== 线程清理 =====================
    close(client_fd);// 关闭客户端连接
    
    printf("Client %s:%d disconnected\n", info->ip, info->port);
    log_info("Client %s:%d disconnected", info->ip, info->port);

    // 更新活跃线程数
    pthread_mutex_lock(&thread_count_mutex);
    active_threads--;
    pthread_mutex_unlock(&thread_count_mutex);

    free(info);// 释放客户端信息结构体
    return NULL;
    
}

// ===================== 主函数：服务端初始化 + 监听 + 接收连接 =====================
int main(){
    
    log_set_level(LOG_LEVEL_INFO);  // 设置日志级别（可选，默认 INFO）
    
    signal(SIGINT, sigint_handler); // 注册信号处理
    
    ensure_server_dir();            //确保服务端文件存储目录存在

    // ===================== 1. 创建服务端 socket =====================
    // 创建 socket，直接赋值给全局变量
    server_fd=socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0){
        perror("socket");
        log_error("socket creation failed");

        exit(1);
    }

    //应用层（socket本身层及），允许本地地址和端口，开启复用，变量opt长度
    int opt=1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        log_error("setsockopt failed");
        close(server_fd);
        exit(1);
    }

    // ===================== 2. bind绑定 IP 和端口 =====================
    //2.1配置服务端地址
    struct sockaddr_in addr;        //ipv4专用
    memset(&addr,0,sizeof(addr));   //清空结构体，或者用bzero
    addr.sin_family=AF_INET;        //IPV4
    addr.sin_addr.s_addr=INADDR_ANY;//监听所有ip
    addr.sin_port=htons(PORT);      //端口（转为网络字节序）
    
    //2.2bind
    if(bind(server_fd,(struct sockaddr *)&addr,sizeof(addr))<0){
        perror("bind");
        log_error("bind failed");
        close(server_fd);
        exit(1);
    }

    // ===================== 3. listen 开始监听=====================
    //正在三次握手的客户端链表数量=2*backlog+1
    if(listen(server_fd,128)<0){
        perror("listen");
        log_error("listen failed");
        close(server_fd);
        exit(1);
    }

    printf("server listening on port %d\n",PORT);
    log_info("Server listening on port %d", PORT);


    // ===================== 4. accept循环接收客户端连接 =====================
    while(running){
        //4.1 accpet
        //client_addr用来存放客户的ip和端口
        struct sockaddr_in client_addr;
        socklen_t addr_len=sizeof(client_addr);

        //阻塞在这里，等客户连接，成功时拿到client_fd,同时把客户ip和端口存入client_addr
        int client_fd=accept(server_fd,(struct sockaddr *)&client_addr,&addr_len);
        if(client_fd<0){
            if (!running) {
                break; // 服务退出
            }
            perror("accept");
            continue;
        }

        //分配客户端信息结构体，为当前客户端分配内存，用malloc是因为要传给子线程
        client_info_t *info = malloc(sizeof(client_info_t));
        if (!info) {
            perror("malloc");
            log_error("malloc failed for client info");
            close(client_fd);
            continue;
        }

        info->client_fd = client_fd;
        if (inet_ntop(AF_INET, &client_addr.sin_addr, info->ip, INET_ADDRSTRLEN) == NULL) {
            perror("inet_ntop");
            log_error("inet_ntop failed");
            free(info);
            close(client_fd);
            continue;
        }

        info->port = ntohs(client_addr.sin_port);

        //上锁，active_threads是全局变量，所有线程都能改，所以要加锁
        pthread_mutex_lock(&thread_count_mutex);
        active_threads++;//活跃线程数 +1
        pthread_mutex_unlock(&thread_count_mutex);

        // 创建线程处理客户端
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, info) != 0) {
            perror("pthread_create");
            log_error("pthread_create failed");
            pthread_mutex_lock(&thread_count_mutex);
            active_threads--;
            pthread_mutex_unlock(&thread_count_mutex);
            free(info);
            close(client_fd);
            continue;
        }

        pthread_detach(tid);// 线程分离，自动释放资源
        
    }
    
    // ===================== 5. 服务退出：等待所有线程结束 =====================
    while (active_threads > 0) {
        sleep(1);
    }
    if (server_fd != -1) {
        close(server_fd);
    }

    printf("Server stopped.\n");
    log_info("Server stopped");
    log_close();   // 关闭日志文件
    return 0;
}