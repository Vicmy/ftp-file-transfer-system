//头文件保护宏
#ifndef PROTOCOL_H
#define PROTOCOL_H
//固定大小整数类型的头文件
#include<stdint.h>
//命令码
#define CMD_UPLOAD 0x01       //1

#define CMD_DOWNLOAD 0x02       //2

#define CMD_QUERY_OFFSET 0x03   //3

#define CMD_ERROR 0xFF          //255

#define DATA_BLOCK_SIZE 4096    //每次收发数据的缓冲区大小=4096(4KB)

// 错误码
#define ERR_FILE_NOT_FOUND  0x01
#define ERR_DISK_FULL       0x02
#define ERR_IO              0x03
#define ERR_PERMISSION      0x04
#endif

