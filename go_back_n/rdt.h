//
// Created by xjs on 2020/3/29.
//

#ifndef RELIABLE_DATA_TRANSFER_PROTOCOL_RTD_H
#define RELIABLE_DATA_TRANSFER_PROTOCOL_RTD_H

#endif //RELIABLE_DATA_TRANSFER_PROTOCOL_RTD_H

#define INIT_HOST 1
#define INIT_SWITCH 2
#define BUFF_SIZE 16384

#include "_rdt.h"

int init_conn(int mode, const char *path);//初始化连接

static int _rdt_init_handle(const char *path);//初始化发送进程
static void _rdt_handle(int proc_fd, int conn_fd);//发送进程主函数
static void *_rdt_send_thread(void *arg);//发送线程
static void *_rdt_recv_thread(void *arg);//接受线程
static void _timeout(void *arg);//超时处理