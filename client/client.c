#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<inttypes.h>

#include"../common/utils.h"
#include "../common/protocol.h"

#define PORT 8888
#define SERVER_IP "127.0.0.1"

int main(int argc,char *argv[]){
    // 检查命令行参数：必须是 客户端名 + 命令(upload/download) + 文件名
    if(argc!=3){
        fprintf(stderr, "Usage: %s <upload|download> <filename>\n", argv[0]);
        exit(1);
    }
    // ===================== 1. 创建客户端 socket =====================
    // AF_INET：IPv4，SOCK_STREAM：TCP，0：默认协议
    int sock=socket(AF_INET,SOCK_STREAM,0);
    if (sock<0){
        perror("socket");
        exit(1);
    }

    // ===================== 2. 连接服务器connect =====================
    //2.1 定义服务器地址结构体
    struct  sockaddr_in addr;
    memset(&addr, 0, sizeof(addr)); 
    addr.sin_family=AF_INET;
    addr.sin_port=htons(PORT);// 端口（转为网络字节序）

     // 2.2将字符串IP转为网络字节序IP，存入 addr.sin_addr
    if (inet_pton(AF_INET, SERVER_IP, &addr.sin_addr) != 1) {
        perror("inet_pton");
        close(sock);
        exit(1);
    }
        
    
    //2.3connect
    if(connect(sock,(struct sockaddr *)&addr,sizeof(addr))<0){
        perror("connect");
        close(sock);
        exit(1);
     
    }
    // ===================== 安全检查：文件名是否合法 =====================
    const char *filename = argv[2];
    if (!is_safe_filename(filename)) {
        fprintf(stderr, "Unsafe filename: %s\n", filename);
        close(sock);
        exit(1);
    }
    //===================== 3. 准备命令与数据传输 =====================
    //3.1确定命令码
    uint8_t name_len = strlen(filename);// 文件名长度（uint8_t）
    uint8_t cmd;// 命令码
    // 3.2判断是上传还是下载
    if(strcmp(argv[1],"upload")==0){//比的是地址
        cmd=CMD_UPLOAD;
    }else if(strcmp(argv[1],"download")==0){
        cmd=CMD_DOWNLOAD;
    }else{
        fprintf(stderr,"Invalid command:%s\n",argv[1]);
        close(sock);
        exit(1);
    }
    // ===================== 分支1：执行 UPLOAD 上传（支持断点续传） =====================
    //3.3如果是上传，还需要发送文件大小和其实偏移
    if(cmd==CMD_UPLOAD){
        // -------------------- 第一步：向服务器查询已上传偏移量 --------------------
        uint8_t query_cmd=CMD_QUERY_OFFSET;
        // 发送：查询命令 + 文件名长度 + 文件名
        if (send_n(sock, &query_cmd, 1) != 1 ||
            send_n(sock, &name_len, 1) != 1 ||
            send_n(sock, filename, name_len) != name_len) {
            perror("send query");
            close(sock);
            exit(1);
        }
    
        //接收服务器返回的 已上传偏移量（8字节）
        uint64_t server_offset;
        if(recv_n(sock,&server_offset,8)!=8){
            perror("recv offset");
            close(sock);
            exit(1);
        }
        printf("Server alreadly has %" PRIu64 "bytes\n",server_offset);

         // -------------------- 第二步：打开本地文件，获取文件大小 --------------------
        FILE *fp=fopen(filename,"rb");//以二进制读打开
        if(!fp){
            perror("fopen");
            close(sock);
            exit(1);
        }
        //跳到文件末尾，获取总大小
         if (fseeko(fp, 0, SEEK_END) != 0) {
            perror("fseeko");
            fclose(fp);
            close(sock);
            exit(1);
        }
       
        uint64_t local_size=ftello(fp);// 获取文件总字节数
        // 跳回文件开头
        if (fseeko(fp, 0, SEEK_SET) != 0) {
            perror("fseeko");
            fclose(fp);
            close(sock);
            exit(1);
        }

        // 如果服务器已经有完整文件，直接退出
        if(server_offset>=local_size){
            printf("File already fully uploaded\n");
            fclose(fp);
            close(sock);
            return 0;
        }
        // 跳转到服务器已接收的位置，开始续传
        if (fseeko(fp, (off_t)server_offset, SEEK_SET) != 0) {
            perror("fseeko");
            fclose(fp);
            close(sock);
            exit(1);
        }

        // 剩余需要上传的字节数
        uint64_t remaining=local_size-server_offset;

        // -------------------- 第三步：发送真正的上传头信息 --------------------
        uint8_t upload_cmd = CMD_UPLOAD;
        if (send_n(sock, &upload_cmd, 1) != 1 ||            // 上传命令
            send_n(sock, &name_len, 1) != 1 ||              // 文件名长度
            send_n(sock, filename, name_len) != name_len || // 文件名
            send_n(sock, &local_size, 8) != 8 ||            // 文件总大小
            send_n(sock, &server_offset, 8) != 8) {         // 起始偏移（续传点）
            perror("send upload header");
            fclose(fp);
            close(sock);
            exit(1);
        }

        // -------------------- 第四步：循环发送文件数据 --------------------
        char buffer[DATA_BLOCK_SIZE];// 数据缓冲区
        size_t bytes;                // 每次读取的字节数
        uint64_t sent = 0;           // 本次已发送字节
        int error=0;                 // 错误标记
        while (sent<remaining&&(bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
            //发送数据
            if(send_n(sock,buffer,bytes)!=bytes){
                perror("send data");
                error=1;
                break;
            }
            
            sent += bytes;
            // 实时打印上传进度（\r 回到行首，覆盖输出）
            printf("\rupload progress: %" PRIu64 " / %" PRIu64 "", server_offset + sent, local_size);
            
            fflush(stdout);
        }
        fclose(fp); // 关闭文件
        // 强制输出最终进度（解决小文件不显示）
        printf("\rupload progress: %" PRIu64 " / %" PRIu64 "", server_offset + sent, local_size);
        
        // 上传结果提示
        if (error) {
            printf("\nUpload failed.\n");
        } else {
            printf("\nUpload finished.\n");
            fflush(stdout);
            sleep(1); 
        }
        
    }
    /// ===================== 分支2：执行 DOWNLOAD 下载（支持断点续传） =====================
    else if(cmd==CMD_DOWNLOAD){
        // -------------------- 第一步：检查本地已下载大小 --------------------
        uint64_t local_offset = 0;
        FILE *fp = fopen(filename, "rb");// 尝试只读打开（判断是否存在）
        if (fp) {
            // 文件存在，获取已下载大小
            if (fseeko(fp, 0, SEEK_END) != 0) {
                perror("fseeko");
                fclose(fp);
                close(sock);
                exit(1);
            }
            local_offset = ftello(fp);
            fclose(fp);
            printf("Local file exists, size: %" PRIu64 "\n", local_offset);
        }
        // -------------------- 第二步：发送下载请求给服务器 --------------------
        uint8_t download_cmd = CMD_DOWNLOAD;
         if (send_n(sock, &download_cmd, 1) != 1 ||         // 下载命令
            send_n(sock, &name_len, 1) != 1 ||               // 文件名长度
            send_n(sock, filename, name_len) != name_len || // 文件名
            send_n(sock, &local_offset, 8) != 8) {          // 本地已下载偏移
            perror("send download header");
            close(sock);
            exit(1);
        }

        // -------------------- 第三步：接收服务器状态（成功/失败） --------------------
        uint8_t status;
        if (recv_n(sock, &status, 1) != 1) {
            perror("recv status");
            close(sock);
            exit(1);
        }

        // 状态=0x01 表示服务器出错
        if (status == 0x01) {
            uint8_t err_code;
            if (recv_n(sock, &err_code, 1) != 1) {
                perror("recv error code");
                close(sock);
                exit(1);
            }
            // 打印错误信息
            fprintf(stderr, "Server error: ");
            switch (err_code) {
                case ERR_FILE_NOT_FOUND: fprintf(stderr, "File not found\n"); break;
                case ERR_DISK_FULL:      fprintf(stderr, "Disk full\n"); break;
                case ERR_IO:             fprintf(stderr, "I/O error\n"); break;
                case ERR_PERMISSION:     fprintf(stderr, "Permission denied\n"); break;
                default:                 fprintf(stderr, "Unknown error (%d)\n", err_code);
            }
             close(sock);
            exit(1);
        }

        // -------------------- 第四步：接收文件总大小 --------------------
        uint64_t file_size;
        if (recv_n(sock, &file_size, 8) != 8) {
            perror("recv file size");
            close(sock);
            exit(1);
        }
        // 本地已下载完整，直接退出
        if (local_offset >= file_size) {
            printf("File already fully downloaded.\n");
            close(sock);
            return 0;
        }
        // -------------------- 第五步：打开文件准备续写 --------------------
        // 先尝试 r+b（可读写，文件必须存在）
        fp = fopen(filename, "r+b");
        if (!fp) {
            fp = fopen(filename, "wb");// 不存在则新建
        }
        if (!fp) {
            perror("fopen");
            close(sock);
            exit(1);
        }

        // 定位到本地已下载位置
        if (fseeko(fp, (off_t)local_offset, SEEK_SET) != 0) {
            perror("fseeko");
            fclose(fp);
            close(sock);
            exit(1);
        }

        // -------------------- 第六步：循环接收数据并写入文件 --------------------
        char buffer[DATA_BLOCK_SIZE];
        uint64_t received = local_offset;
        int error = 0;

        while (received < file_size) {
            // 本次需要接收的长度（不超过缓冲区）
            int to_read = (file_size - received) > DATA_BLOCK_SIZE ? DATA_BLOCK_SIZE : (file_size - received);
            int n = recv_n(sock, buffer, to_read);//接收一段数据
            
            if (n != to_read) {
                printf("\nError: expected %d bytes, got %d\n", to_read, n);
                error = 1;
                break;
            }

            //写入文件
            size_t written = fwrite(buffer, 1, n, fp);
            if (written != n) {
                perror("fwrite");
                error = 1;
                break;
            }
            
            received += n;//累计已接收
            // 实时打印下载进度
            printf("\rDownload progress: %" PRIu64 " / %" PRIu64 "", received, file_size);
            fflush(stdout);
        }
        fclose(fp);
        // 强制输出最终下载进度（解决小文件不显示）
        printf("\rDownload progress: %" PRIu64 " / %" PRIu64 "", received, file_size);

         // 下载结果提示
        if (error) {
            printf("\nDownload failed.\n");
        } else {
            printf("\nDownload finished.\n");
            sleep(1);
        }
    }


    // ===================== 4. 关闭 socket，结束程序 =====================
    close(sock);
    return 0;

}