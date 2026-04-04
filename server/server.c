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
#define RECV_TIMEOUT_SEC 5   // MODIFIED: recv 超时时间（秒），用于定期检查 running 标志

// 全局变量用于优雅退出
static volatile int running = 1;          // 主循环标志
static int server_fd = -1;                // 监听 socket（全局，供信号处理函数关闭）
static pthread_mutex_t thread_count_mutex = PTHREAD_MUTEX_INITIALIZER;
static int active_threads = 0;            // 当前活跃线程数

//线程参数结构体，传递客户端信息
typedef struct{
    int client_fd;//客户端socket
    char ip[INET_ADDRSTRLEN];//客户端ip
    int port;//客户端端口
}client_info_t;
//创建存储目录
static void ensure_server_dir(){
    if (mkdir("./server_files", 0755) != 0 && errno != EEXIST) {
        perror("mkdir");
        // 目录创建失败不影响程序继续，但可能后续文件操作失败
        log_error("Failed to create server_files directory: %s", strerror(errno));

    }
}

//信号处理函数：捕获 SIGINT，设置退出标志并关闭监听 socket
 
static void sigint_handler(int sig) {
    (void)sig;//参数存在不用它
    log_info("Received SIGINT, shutting down...");
    running = 0;// 仅设置标志，让线程和主循环自然退出
    if (server_fd != -1) {//有效才关闭
        close(server_fd);
        server_fd = -1;
    }
}

// 辅助函数：为 socket 设置接收超时，客户端连接一直不说话
static void set_recv_timeout(int fd, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;//微秒，0就是没有，只按照秒
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}


//线程函数，处理单个客户端所有请求
static void *handle_client(void *arg){
    //解析传入的客户端信息结构体，pthread_create的参数只能是void，它失忆了原来的类型。
    client_info_t *info=(client_info_t *)arg;
    int client_fd=info->client_fd;
    printf("Client %s:%d connected\n",info->ip,info->port);
    log_info("Client %s:%d connected", info->ip, info->port);
     // 设置接收超时，使 recv 能定期返回检查 running
    set_recv_timeout(client_fd, RECV_TIMEOUT_SEC);


    int client_alive=1;//控制是否继续处理命令
    //循环处理客户端命令(上传/下载/查询)
    while(client_alive && running){
        //5.1 接收cmd(1字节)
        uint8_t cmd;

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
        switch (cmd){
            //查询服务端文件大小
            case CMD_QUERY_OFFSET:{
                //接收文件名
                uint8_t name_len;
                if (recv_n(client_fd, &name_len, 1) != 1) {
                    log_warning("Failed to receive name length from %s:%d", info->ip, info->port);

                    client_alive = 0;
                    break;
                }
                char filename[256];
                if (recv_n(client_fd, filename, name_len) != name_len) {
                    log_warning("Failed to receive filename from %s:%d", info->ip, info->port);

                    client_alive = 0;
                    break;
                }
                filename[name_len] = '\0';//字符串结束

                if(!is_safe_filename(filename)){
                    log_warning("Unsafe filename from %s:%d: %s", info->ip, info->port, filename);
             
                    client_alive=0;
                    break;
                }

                char filepath[512];
                snprintf(filepath, sizeof(filepath), "./server_files/%s", filename);
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
            //上传文件
            case CMD_UPLOAD:{
                //5.2接收文件名长度
                uint8_t name_len;
                if(recv_n(client_fd,&name_len,1)!=1){
                    log_warning("Failed to receive name length from %s:%d", info->ip, info->port);

                    client_alive = 0;
                    break;

                }
                //5.3接收文件名
                char filename[256];
                if(recv_n(client_fd,filename,name_len)!=name_len){
                    log_warning("Failed to receive filename from %s:%d", info->ip, info->port);

                    client_alive = 0;
                    break;
                }
                filename[name_len]='\0';
                if(!is_safe_filename(filename)){
                    log_warning("Unsafe filename from %s:%d: %s", info->ip, info->port, filename);

                    client_alive=0;
                    break;
                }
            
                //5.4接收文件大小
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
                //printf("Upload :file_size=%llu,offset=%llu\n",file_size,offset);
                printf("Upload: %s, size=%" PRIu64 ", offset=%" PRIu64 "\n",
                       filename, file_size, offset);
                log_info("Upload from %s:%d: %s, size=%" PRIu64 ", offset=%" PRIu64,
                         info->ip, info->port, filename, file_size, offset);
                
                char filepath[512];//放完整文件名
                snprintf(filepath,sizeof(filepath),"./server_files/%s",filename);
                
                //打开文件
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
                
                //防止多线程同时写一个文件
                int fd = fileno(fp);
                if (flock(fd, LOCK_EX) != 0) {
                    perror("flock");
                    log_error("flock failed for %s", filename);

                    fclose(fp);
                    client_alive = 0;
                    break;
                }
                // 定位到偏移
                if (fseeko(fp, (off_t)offset, SEEK_SET) != 0) {
                    perror("fseeko");
                    log_error("fseeko failed for %s at offset %" PRIu64, filename, offset);

                    flock(fd, LOCK_UN);
                    fclose(fp);
                    client_alive = 0;
                    break;
                }
                

                //接受数据
            
                uint64_t received=offset;
                char buffer[DATA_BLOCK_SIZE];//4096。一次存4kb
                int error = 0;
                while(received<file_size){
                    int to_read = DATA_BLOCK_SIZE;
                    if (file_size - received < DATA_BLOCK_SIZE) {
                        to_read = file_size - received;
                    }
                    int n = recv_n(client_fd, buffer, to_read);
                    if (n != to_read) {
                        // 如果连接关闭且已接收完整，则视为成功
                        if (n == 0 && received == file_size) {
                            break;
                        }
                        error = 1;
                        break;
                    }
                    size_t written = fwrite(buffer, 1, n, fp);
                    if (written != n) {
                        perror("fwrite");
                        error = 1;
                        break;
                    }
                    received += n;
                }
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
            
                
                
            case CMD_DOWNLOAD:{

                //5.2接收文件名长度
                uint8_t name_len;
                if(recv_n(client_fd,&name_len,1)!= 1){
                    printf("Falied to receive name length\n");
                    client_alive = 0;
                    break;

                }
                //5.3接收文件名
                char filename[256];
                if(recv_n(client_fd,filename,name_len)!= name_len){
                    log_warning("Failed to receive name length from %s:%d", info->ip, info->port);

                    client_alive = 0;
                    break;
                }
                filename[name_len]='\0';
                if(!is_safe_filename(filename)){
                    log_warning("Failed to receive filename from %s:%d", info->ip, info->port);
             
                    client_alive=0;
                    break;
                }
                

                //5.4接收起始偏移
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
                FILE *fp = fopen(filepath, "rb");//只读
                if (!fp) {
                    perror("fopen");
                    log_error("File not found: %s", filename);

                    // 发送文件大小为0表示错误
                    uint8_t status = 0x01;      // 错误状态
                    uint8_t err_code = ERR_FILE_NOT_FOUND;
                    send_n(client_fd, &status, 1);
                    send_n(client_fd, &err_code, 1);
                    client_alive = 0;
                    break;
                }
                //获取文件大小

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

                if (fseeko(fp, (off_t)offset, SEEK_SET) != 0) {//跳回偏移位置
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
                
                
                // 发送 成功状态 + 文件大小
                uint8_t status = 0x00;   // 成功
                if (send_n(client_fd, &status, 1) != 1 ||
                    send_n(client_fd, &file_size, 8) != 8) {
                    perror("send download header");
                    log_error("Failed to send download header for %s", filename);

                    fclose(fp);
                    client_alive = 0;
                    break;
                }
               
                
                //发送数据
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
                
                
            
            default:
                printf("Unknown commond:%d\n",cmd);
                log_warning("Unknown command %d from %s:%d", cmd, info->ip, info->port);

                client_alive = 0;
                break;
        }
    }
    //5.3关闭客户端连接
    close(client_fd);
    
    printf("Client %s:%d disconnected\n", info->ip, info->port);
    log_info("Client %s:%d disconnected", info->ip, info->port);

    pthread_mutex_lock(&thread_count_mutex);
    active_threads--;
    pthread_mutex_unlock(&thread_count_mutex);
    free(info);
    return NULL;
    
}

int main(){
    // 设置日志级别（可选，默认 INFO）
    log_set_level(LOG_LEVEL_INFO);
    // 注册信号处理
    signal(SIGINT, sigint_handler);
    //确保服务端文件存储目录存在
    ensure_server_dir();
    //1.socket
    // 创建 socket，直接赋值给全局变量
    server_fd=socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0){
        perror("socket");
        log_error("socket creation failed");

        exit(1);
    }
    //
    int opt=1;
    //应用层（socket本身层及），允许本地地址和端口，开启复用，变量opt长度
    
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        log_error("setsockopt failed");
        close(server_fd);
        exit(1);
    }

    //2.bind
    //2.1配置服务端地址
    struct sockaddr_in addr;//ipv4专用
    memset(&addr,0,sizeof(addr));//清空结构体，或者用bzero
    addr.sin_family=AF_INET;//IPV4
    addr.sin_addr.s_addr=INADDR_ANY;//监听所有ip
    addr.sin_port=htons(PORT);//8888
    //2.2bind

    if(bind(server_fd,(struct sockaddr *)&addr,sizeof(addr))<0){
        perror("bind");
        log_error("bind failed");
        close(server_fd);
        exit(1);
    }

    //3.listen
    //正在三次握手的客户端链表数量=2*backlog+1
    if(listen(server_fd,128)<0){
        perror("listen");
        log_error("listen failed");
        close(server_fd);
        exit(1);
    }
    printf("server listening on port %d\n",PORT);
    log_info("Server listening on port %d", PORT);


    //4.accept
    while(running){
        //4.1 accpet
        //client_addr用来存放客户的ip和端口
        struct sockaddr_in client_addr;
        socklen_t addr_len=sizeof(client_addr);
        //阻塞在这里，等客户连接，成功时拿到client_fd,同时把客户ip和端口存入client_addr
        int client_fd=accept(server_fd,(struct sockaddr *)&client_addr,&addr_len);
        if(client_fd<0){
            if (!running) {
                break; // 正常退出
            }
            perror("accept");
            continue;
        }

        //为当前客户端分配内存，用malloc是因为要传给子线程
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
        active_threads++;
        pthread_mutex_unlock(&thread_count_mutex);

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
        pthread_detach(tid);
        
    }
    
    // 等待所有客户端线程结束
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