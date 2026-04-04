#ifndef UTILS_H
#define UTILS_H

//可靠接受len字节，返回实际接收字节数（出错返回-1，连接关闭返回0）
int recv_n(int sockfd,void *buf,int len);

//可靠发送len字节，返回实际发送字节数（出错返回-1）
int send_n(int sockfd,const void *buf,int len);

// 安全检查：防止路径遍历
int is_safe_filename(const char *filename);

#endif