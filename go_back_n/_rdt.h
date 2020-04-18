//
// Created by xjs on 2020/3/29.
//

#ifndef RELIABLE_DATA_TRANSFER_PROTOCOL__RTD_H
#define RELIABLE_DATA_TRANSFER_PROTOCOL__RTD_H

#endif //RELIABLE_DATA_TRANSFER_PROTOCOL__RTD_H

#include <stddef.h>
#include <time.h>
#include <pthread.h>

#define PACKET_MAX_SIZE sizeof(struct Packet)
#define PACKET_HDR_SIZE sizeof(struct Packet_hdr)
#define PACKET_DATA_SIZE 4096
#define _is_ack(pkt) pkt.hdr.ack
#define _get_seq(pkt) pkt.hdr.seqnum
#define _is_correct(pkt) _checksum(&pkt, pkt.hdr.len + PACKET_HDR_SIZE)


struct Packet_hdr{
    int seqnum;
    unsigned short checksum;
    char ack;
    size_t len;
};
struct Packet{
    struct Packet_hdr hdr;
    char data[PACKET_DATA_SIZE];
};

int _init_conn(const char *path);
int _open_conn(const char *path);
struct Packet _make_pkt(int seq, void *data, char ack, size_t len);
size_t _unmake_pkt(struct Packet *pkt, void *buf, size_t len);
unsigned short _checksum(void *data, size_t len);

struct Time_argv{
    time_t time;//睡眠时间
    void (*func)(void*);//调用函数
    void *data;//数据
};
pthread_t _timer(time_t time/*ms*/, void (*func)(void*), void *data);
static void *_timer_thread(void *argv);