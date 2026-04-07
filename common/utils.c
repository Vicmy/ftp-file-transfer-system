#include"utils.h"

#include<unistd.h>
#include<sys/socket.h>
#include<string.h>
#include<errno.h>

/**
 * 功能：可靠接收指定长度的数据（解决TCP粘包/分包问题）
 * 参数：sockfd - 套接字；buf - 接收缓冲区；len - 期望接收的总字节数
 * 返回：成功返回总接收长度；失败返回-1；对端关闭返回已接收长度
 */
int recv_n(int sockfd,void *buf,int len){
    int total=0;        // 已经成功接收的字节数
    int n;              // 每次调用recv实际收到的字节数
    char *ptr=(char *)buf;// 用char*指向缓冲区，方便字节级移动

    // 循环直到收满 len 字节
    while(total<len){
        //ptr+total：从缓冲区当前位置继续写
        //len-total：还剩多少字节没收
        n=recv(sockfd,ptr+total,len-total,0);
        
        if (n == -1) {
            if (errno == EAGAIN || errno == EINTR) {
                // 超时或信号中断，继续重试（不返回错误）
                continue;
            }
            return -1;  // 真正错误
        }
        if(n==0) // 客户端关闭连接，返回已经收到的字节数
            return total;
        
        total+=n;// 累计已接收字节
    }
    return total;// 收满，返回总长度

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

/**
 * 功能：检查文件名是否安全，防止路径穿越攻击
 * 规则：禁止 / \ .. 等危险字符
 * 返回：1=安全；0=不安全
 */
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