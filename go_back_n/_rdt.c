//
// Created by xjs on 2020/3/29.
//

#include "_rdt.h"
#include <sys/stat.h>
#include <memory.h>
#include <stdlib.h>
#include <zconf.h>
#include <sys/socket.h>
#include <sys/un.h>

int _init_conn(const char *path)
{
    struct sockaddr_un un;
    int fd, size;
    if(strlen(path) >= sizeof(un.sun_path))
        return -1;
    un.sun_family = AF_UNIX;
    strcpy(un.sun_path, path);
    unlink(path);
    if((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
        return -1;
    size = offsetof(struct sockaddr_un, sun_path) + strlen(un.sun_path);
    if(bind(fd, (struct sockaddr *)&un, size) < 0)
        return -1;
    if(listen(fd, 1) < 0){
        close(fd);
        return -1;
    }
    return fd;
}

int _open_conn(const char *path)
{
    int fd, size;
    struct sockaddr_un un;
    if(strlen(path) >= sizeof(un.sun_path))
        return -1;
    un.sun_family = AF_UNIX;
    strcpy(un.sun_path, path);
    if((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
        return -1;
    size = offsetof(struct sockaddr_un, sun_path) + strlen(un.sun_path);
    if(connect(fd, (struct sockaddr *)&un, size) < 0)
        return -1;
    return fd;
}

struct Packet _make_pkt(int seq, void *data, char ack, size_t len)
{
    struct Packet pkt;
    bzero(&pkt, PACKET_MAX_SIZE);
    pkt.hdr.ack = ack;
    pkt.hdr.len = len;
    pkt.hdr.seqnum = seq;
    pkt.hdr.checksum = 0;
    if(data != NULL)
        bcopy(data, &pkt.data, len);
    pkt.hdr.checksum = _checksum(&pkt, len + PACKET_HDR_SIZE);
    return pkt;
}

size_t _unmake_pkt(struct Packet *pkt, void *buf, size_t len)
{
    if(len < pkt->hdr.len)
        return -1;
    bcopy(pkt->data, buf, pkt->hdr.len);
    return pkt->hdr.len;
}

pthread_t _timer(time_t time, void (*func)(void*), void *data)
{
    int err;
    pthread_attr_t attr;
    pthread_t tid;
    struct Time_argv *arg = malloc(sizeof(struct Time_argv));
    if(arg == NULL)
        return -1;
    arg->func = func;
    arg->data = data;
    arg->time = time;
    err = pthread_attr_init(&attr);
    if(err != 0)
        goto ERR;
    err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);//分离线程状态
    if(err < 0)
        goto ERR;
    err = pthread_create(&tid, &attr, _timer_thread, arg);
    if(err < 0)
        goto ERR;
    pthread_attr_destroy(&attr);
    return tid;
    ERR:
        free(arg);
        return -1;
}

static void *_timer_thread(void *argv)
{
    pthread_cleanup_push(free, argv);//设置清理函数
    struct Time_argv *t_arg = (struct Time_argv *)argv;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);//启用线程可以被取消
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);//异步取消
    usleep(t_arg->time * 1000);
    t_arg->func(t_arg->data);
    pthread_cleanup_pop(1);//参数设为非0不管是否被取消都调用清理函数
    return NULL;
}

unsigned short _checksum(void *data, size_t len)
{
    unsigned short *p_data = data;
    unsigned int sum = 0;
    unsigned short *p_sum = (unsigned short*)&sum;
    do
    {
        sum += *p_data++;
    } while((len -= 2) > 1);
    if(len == 1)
        sum += *(unsigned char*)p_data;
    while(p_sum[1] > 0)//此为小段字节序
    {
        unsigned short tmp = p_sum[1];
        p_sum[1] = 0;
        sum += tmp;
    }
    return ~p_sum[0];
}