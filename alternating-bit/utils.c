//
// Created by xjs on 2020/3/29.
//

#include "utils.h"
int init_buffer(struct Buffer *buff, size_t size)
{
    buff->start = 0;
    buff->end = 0;
    buff->size = size;
    buff->len = 0;
    if(pthread_mutex_init(&buff->lock, NULL) != 0)
        return -1;
    if(pthread_cond_init(&buff->cond, NULL) != 0)
        return -1;
    buff->data = malloc(size);
    if(buff->data == NULL)
        return -1;
    return 0;
}

int destory_buffer(struct Buffer *buff)
{
    buff->start = 0;
    buff->end = 0;
    buff->size = 0;
    if(pthread_mutex_destroy(&buff->lock) != 0)
        return -1;
    if(pthread_cond_destroy(&buff->cond) != 0)
        return -1;
    free(buff->data);
    return 0;
}

int read_buffer(struct Buffer *buff, void *dst, size_t len)
{
    int n;
    if(buff->size == 0)
        return -1;
    if(pthread_mutex_lock(&buff->lock) != 0)//上锁，保护临界区
        return -1;
    if(buff->len == 0){//如果缓存区没有数据，则等待条件完成
        //printf("read locak\n");
        if(pthread_cond_wait(&buff->cond, &buff->lock) != 0) {
            pthread_mutex_unlock(&buff->lock);
            return -1;
        }
    }
    n = MIN(buff->len, len);
    if(buff->start + n <= buff->size) {
        bcopy(buff->data + buff->start, dst, n);
    } else{//读取到达末尾，从头继续读
        size_t temp = buff->size - buff->start;
        bcopy(buff->data + buff->start, dst, temp);
        bcopy(buff->data, dst + temp, n - temp);
    }
    buff->start = (buff->start + n) % (int)buff->size;
    buff->len -= n;
    pthread_mutex_unlock(&buff->lock);
    return n;
}

int write_buffer(struct Buffer *buff, void *src, size_t len)
{
    int n;
    if(buff->size == 0)
        return -1;
    if(pthread_mutex_lock(&buff->lock) != 0)
        return -1;
    if(buff->len == buff->size){
        //printf("write locak\n");
        if(pthread_cond_wait(&buff->cond, &buff->lock) != 0) {
            pthread_mutex_unlock(&buff->lock);
            return -1;
        }
    }
    n = MIN(buff->size - buff->len, len);
    if(buff->end + n <= buff->size){
        bcopy(src, buff->data + buff->end, n);
    } else{
        size_t temp = buff->size - buff->end;
        bcopy(src, buff->data + buff->end, temp);
        bcopy(src + temp, buff->data, n - temp);
    }
    buff->end = (buff->end + n) % (int)buff->size;
    buff->len += n;
    pthread_mutex_unlock(&buff->lock);
    return n;
}