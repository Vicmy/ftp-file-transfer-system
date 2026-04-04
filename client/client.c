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
    if(argc!=3){
        fprintf(stderr, "Usage: %s <upload|download> <filename>\n", argv[0]);
        exit(1);
    }
    //1.socket
    int sock=socket(AF_INET,SOCK_STREAM,0);
    if (sock<0){
        perror("socket");
        exit(1);
    }

    //2.connect
    //2.1 客户端地址信息
    struct  sockaddr_in addr;
    memset(&addr, 0, sizeof(addr)); 
    addr.sin_family=AF_INET;
    addr.sin_port=htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &addr.sin_addr) != 1) {
        perror("inet_pton");
        close(sock);
        exit(1);
    }
        
    
    //2.2connect
    if(connect(sock,(struct sockaddr *)&addr,sizeof(addr))<0){
        perror("connect");
        close(sock);
        exit(1);
     
    }

    const char *filename = argv[2];
    if (!is_safe_filename(filename)) {
        fprintf(stderr, "Unsafe filename: %s\n", filename);
        close(sock);
        exit(1);
    }
    //3客户端读写
    //3.1确定命令码
    uint8_t name_len = strlen(filename);
    uint8_t cmd;
    if(strcmp(argv[1],"upload")==0){//比的是地址
        cmd=CMD_UPLOAD;


    }else if(strcmp(argv[1],"download")==0){
        cmd=CMD_DOWNLOAD;

    }else{
        fprintf(stderr,"Invalid command:%s\n",argv[1]);
        close(sock);
        exit(1);
    }
    //3.2 
    
    //3.3如果是上传，还需要发送文件大小和其实偏移
    if(cmd==CMD_UPLOAD){
        //断点续传，查询服务器已有偏移
        uint8_t query_cmd=CMD_QUERY_OFFSET;
        if (send_n(sock, &query_cmd, 1) != 1 ||
            send_n(sock, &name_len, 1) != 1 ||
            send_n(sock, filename, name_len) != name_len) {
            perror("send query");
            close(sock);
            exit(1);
        }
        

        //接收服务器返回的偏移量
        uint64_t server_offset;
        if(recv_n(sock,&server_offset,8)!=8){
            perror("recv offset");
            close(sock);
            exit(1);
        }
        printf("Server alreadly has %" PRIu64 "bytes\n",server_offset);

        //打开本地文件
        FILE *fp=fopen(filename,"rb");//以二进制读打开
        if(!fp){
            perror("fopen");
            close(sock);
            exit(1);
        }
        //获取文件大小
         if (fseeko(fp, 0, SEEK_END) != 0) {
            perror("fseeko");
            fclose(fp);
            close(sock);
            exit(1);
        }//跳到文件末尾
       
        uint64_t local_size=ftello(fp);//获取文件大小
        if (fseeko(fp, 0, SEEK_SET) != 0) {
            perror("fseeko");
            fclose(fp);
            close(sock);
            exit(1);
        }

        //如果服务端已经上传完了，直接退出
        if(server_offset>=local_size){
            printf("File already fully uploaded\n");
            fclose(fp);
            close(sock);
            return 0;
        }
        //跳转到服务端已有位置，开始续传
        if (fseeko(fp, (off_t)server_offset, SEEK_SET) != 0) {
            perror("fseeko");
            fclose(fp);
            close(sock);
            exit(1);
        }
        //剩余要传字节
        uint64_t remaining=local_size-server_offset;

        //发送真名的上传命令及文件信息
        uint8_t upload_cmd = CMD_UPLOAD;
        if (send_n(sock, &upload_cmd, 1) != 1 ||
            send_n(sock, &name_len, 1) != 1 ||
            send_n(sock, filename, name_len) != name_len ||
            send_n(sock, &local_size, 8) != 8 ||
            send_n(sock, &server_offset, 8) != 8) {
            perror("send upload header");
            fclose(fp);
            close(sock);
            exit(1);
        }

        //发送剩余数据
        char buffer[DATA_BLOCK_SIZE];
        size_t bytes;
        uint64_t sent = 0;
        int error=0;
        while (sent<remaining&&(bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
            if(send_n(sock,buffer,bytes)!=bytes){
                perror("send data");
                error=1;
                break;
            }
            
            sent += bytes;
            
            printf("\rupload progress: %" PRIu64 " / %" PRIu64 "", server_offset + sent, local_size);
            
            fflush(stdout);
        }
        fclose(fp);
        // 强制输出最终进度（解决小文件不显示）
        printf("\rupload progress: %" PRIu64 " / %" PRIu64 "", server_offset + sent, local_size);

        if (error) {
            printf("\nUpload failed.\n");
        } else {
            printf("\nUpload finished.\n");
            fflush(stdout);
            sleep(1); 
        }
        
    }
    //
    else if(cmd==CMD_DOWNLOAD){
        //断点续传，检查本地是否已有文件，获取已下载大小
        uint64_t local_offset = 0;
        FILE *fp = fopen(filename, "rb");
        if (fp) {
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
        //发送下在命令和文件名
        uint8_t download_cmd = CMD_DOWNLOAD;
         if (send_n(sock, &download_cmd, 1) != 1 ||
            send_n(sock, &name_len, 1) != 1 ||
            send_n(sock, filename, name_len) != name_len ||
            send_n(sock, &local_offset, 8) != 8) {
            perror("send download header");
            close(sock);
            exit(1);
        }

        // 接收状态字节
        uint8_t status;
        if (recv_n(sock, &status, 1) != 1) {
            perror("recv status");
            close(sock);
            exit(1);
        }

        if (status == 0x01) {
            // 错误状态，读取错误码
            uint8_t err_code;
            if (recv_n(sock, &err_code, 1) != 1) {
                perror("recv error code");
                close(sock);
                exit(1);
            }
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

        // 接收服务器返回的文件总大小
        uint64_t file_size;
        if (recv_n(sock, &file_size, 8) != 8) {
            perror("recv file size");
            close(sock);
            exit(1);
        }
        //如果本地已经下完，直接退出
        if (local_offset >= file_size) {
            printf("File already fully downloaded.\n");
            close(sock);
            return 0;
        }
         // 打开本地文件准备追加（"ab" 模式，并定位到偏移）
        fp = fopen(filename, "r+b");
        if (!fp) {
            fp = fopen(filename, "wb");
        }
        if (!fp) {
            perror("fopen");
            close(sock);
            exit(1);
        }
        if (fseeko(fp, (off_t)local_offset, SEEK_SET) != 0) {
            perror("fseeko");
            fclose(fp);
            close(sock);
            exit(1);
        }

        
        // 接收数据并写入本地文件

        char buffer[DATA_BLOCK_SIZE];
        uint64_t received = local_offset;
        int error = 0;
        while (received < file_size) {
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
            printf("\rDownload progress: %" PRIu64 " / %" PRIu64 "", received, file_size);
            fflush(stdout);
        }
        fclose(fp);
        // 强制输出最终下载进度（解决小文件不显示）
        printf("\rDownload progress: %" PRIu64 " / %" PRIu64 "", received, file_size);


        if (error) {
            printf("\nDownload failed.\n");
        } else {
            printf("\nDownload finished.\n");
            sleep(1);
        }
    }


    

    //4close
    close(sock);
    return 0;

}