//
// Created by xjs on 2020/3/29.
//

#ifndef RELIABLE_DATA_TRANSFER_PROTOCOL_UTILS_H
#define RELIABLE_DATA_TRANSFER_PROTOCOL_UTILS_H

#endif //RELIABLE_DATA_TRANSFER_PROTOCOL_UTILS_H
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

struct Buffer{
    int start;
    int end;
    size_t size;
    size_t len;
    char *data;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

int init_buffer(struct Buffer *buff, size_t size);
int destory_buffer(struct Buffer *buff);
int read_buffer(struct Buffer *buff, void *dst, size_t len);
int write_buffer(struct Buffer *buff, void *src, size_t len);