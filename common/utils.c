#include"utils.h"

#include<unistd.h>
#include<sys/socket.h>
#include<string.h>
#include<errno.h>

int recv_n(int sockfd,void *buf,int len){
    int total=0;
    int n;//每次实际接收的字节数
    char *ptr=(char *)buf;//指向缓冲区当前位置

    while(total<len){
        //ptr+total：从缓冲区当前位置继续写
        //len-total：还剩多少字节没收
        n=recv(sockfd,ptr+total,len-total,0);
        if (n == -1) {
            if (errno == EAGAIN || errno == EINTR) {
                // 超时或信号中断，继续重试（不返回错误）
                continue;
            }
            return -1;  // 其他错误
        }
        if(n==0)//对方关闭连接，返回受到的长度
            return total;
        
        total+=n;//累加
    }
    return total;//返回本次收到的长度

}

int send_n(int sockfd,const void *buf,int len){
    int total=0;
    int n;
    const char *ptr=(const char *)buf;
    while(total<len){
        n=send(sockfd,ptr+total,len-total,0);
        if (n == -1) {
            if (errno == EAGAIN || errno == EINTR) continue;
            return -1;
        }
        total+=n;
    }
    return total;
}

//检查文件名是否安全
int is_safe_filename(const char *filename){
    if(filename==NULL)
        return 0;
    if(strchr(filename,'/')!=NULL)//防止构造类似路径
        return 0 ;
    if(strchr(filename,'\\')!=NULL)//windows路径分隔符
        return 0;
    if(strstr(filename,"..")!=NULL)//防止..路径穿越攻击
        return 0;
    return 1;//可以拼接到./server_files/后面
}