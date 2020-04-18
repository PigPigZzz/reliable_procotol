//
// Created by xjs on 2020/3/29.
//

#include "rdt.h"
#include "utils.h"
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>

#define timer_start(ms, data) _timer(ms, _timeout, data)
#define timer_stop(tid) pthread_cancel(tid)

int init_conn(int mode, const char *path)
{
    switch (mode)
    {
        case INIT_SWITCH: return _init_conn(path);
        case INIT_HOST: return _rdt_init_handle(path);
        default: return -1;
    }
}

static int _rdt_init_handle(const char *path)
{
    int proc_fds[2], conn_fd;
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, proc_fds) < 0)
        return -1;
    pid_t pid;
    if((conn_fd = _open_conn(path)) < 0)
        goto ERR_CONN;
    if((pid = fork()) < 0)
        goto ERR;
    else if(pid != 0){//father
        close(conn_fd);
        close(proc_fds[1]);
        return proc_fds[0];
    } else{//CHILD
        //printf("ppid %d pid %d\n", getppid(), getpid());
        close(proc_fds[0]);
        _rdt_handle(proc_fds[1], conn_fd);
        _exit(0);
    }
    ERR:
        close(conn_fd);
    ERR_CONN:
        close(proc_fds[0]);
        close(proc_fds[1]);
        return -1;
}

static pthread_mutex_t ack_lock = PTHREAD_MUTEX_INITIALIZER;//发送函数发送一个pakcet等待确认之前被锁住
static struct Buffer send_buff, recv_buff;//接受，发送数据缓存
int send_seq = 0, recv_seq = 0;

static void _rdt_handle(int proc_fd, int conn_fd)
{
    fd_set template_set, read_set, write_set;
    struct timeval template_time, tv;
    init_buffer(&send_buff, BUFF_SIZE);
    init_buffer(&recv_buff, BUFF_SIZE);
    char buff[BUFF_SIZE];
    pthread_t send_tid, recv_tid;
    pthread_create(&send_tid, NULL, _rdt_send_thread, &conn_fd);
    pthread_create(&recv_tid, NULL, _rdt_recv_thread, &conn_fd);
    FD_ZERO(&template_set);
    FD_SET(proc_fd, &template_set);
    template_time.tv_sec = 0;
    template_time.tv_usec = 100;
    for(;;){
        read_set = template_set;//从父进程读取数据
        write_set = template_set;//向父进程写入数据
        tv = template_time;
        //printf("proc %d select\n", getpid());
        if(select(proc_fd + 1, &read_set, NULL, NULL, &tv) > 0 && send_buff.size > send_buff.len){
            //printf("proc read %d\n", getpid());
            int n = read(proc_fd, buff, send_buff.size - send_buff.len);//为了让读出来的数据完全写入缓存
            //printf("proc %d read %d bytes %s\n", getpid(), n, buff);
            fprintf(stderr, "befor write send_buff %d\n", send_buff.len);
            write_buffer(&send_buff, buff, n);
            fprintf(stderr, "after write send_buff %d\n", send_buff.len);
            fprintf(stderr, "write send_buff %d bytes\n", n);
            pthread_cond_signal(&send_buff.cond);//通知发送进程有数据准备好
        }
        tv = template_time;
        if(select(proc_fd + 1, NULL, &write_set, NULL, &tv) > 0 && recv_buff.len > 0){
            //printf("proc write %d %d\n", getpid(), recv_buff.len);
            fprintf(stderr, "befor read recv_buff %d\n", recv_buff.len);
            int n = read_buffer(&recv_buff, buff, BUFF_SIZE);
            fprintf(stderr, "after read recv_buff %d\n", recv_buff.len);
            fprintf(stderr, "read recv_buff %d bytes\n", n);
            pthread_cond_signal(&recv_buff.cond);
            //printf("recv_buff.len %d\n", recv_buff.len);
            //printf("proc %d write %d byte %s\n", getpid(), n, buff);
            write(proc_fd, buff, n);
        }
    }
    pthread_join(send_tid, NULL);
    pthread_join(recv_tid, NULL);
}

struct Timeout_data{
    pthread_t tid;//计时线程
    int fd;//重传时的文件描述符
    pthread_mutex_t lock;
    struct Packet pkt;//重传包
};

static void *_rdt_send_thread(void *arg)
{
    int fd = *(int*)arg;
    struct Packet pkt;
    char buff[PACKET_DATA_SIZE];
    struct Timeout_data timeout_data;
    timeout_data.fd = fd;
    pthread_mutex_init(&timeout_data.lock, NULL);
    pthread_mutex_lock(&ack_lock);
    for(;;){
        //printf("want to send %d\n", getpid());
        int n = read_buffer(&send_buff, buff, sizeof(buff));//从缓冲区读取数据，如果没有数据会被锁住
        //printf("read send_buff %d byte %s %d\n", n, buff, getpid());
        pthread_cond_signal(&send_buff.cond);//通知要写入缓冲区但没有空间的进程，可以写入数据了
        pkt = _make_pkt(send_seq, buff, 0, n);
        timeout_data.pkt = pkt;
        timeout_data.tid = timer_start(500, &timeout_data);
        write(fd, &pkt, PACKET_MAX_SIZE);
        //printf("%d send lock\n", getpid());
        pthread_mutex_lock(&ack_lock);//把自己锁住
        //取消计时器
        pthread_mutex_lock(&timeout_data.lock);
        timer_stop(timeout_data.tid);
        pthread_mutex_unlock(&timeout_data.lock);

        send_seq = (send_seq + 1) % 2;
    }
}

static void *_rdt_recv_thread(void *arg)
{
    int fd = *(int*)arg;
    struct Packet pkt;
    char buff[PACKET_MAX_SIZE];
    for(;;){
        //printf("want to read %d\n", getpid());
        int n = read(fd, &pkt, PACKET_MAX_SIZE);
        //printf("read recv_buff %d byte %d\n", n, getpid());
        //printf("check %d %d isack:%d\n", _is_correct(pkt), getpid(), _is_ack(pkt));
        if(!_is_correct(pkt)){
            if(_is_ack(pkt)){
                //printf("is ack seq:%d %d\n", _get_seq(pkt), getpid());
                if (_get_seq(pkt) == send_seq)//如果接受的ack序号和之前发送的序号一致，则解锁发送进程
                    pthread_mutex_unlock(&ack_lock);
            } else{
                //printf("is data seq:%d %d\n", _get_seq(pkt), getpid());
                struct Packet ack_pkt = _make_pkt(_get_seq(pkt), NULL, 1, 0);//回送一个ack
                write(fd, &ack_pkt, PACKET_MAX_SIZE);
                if(_get_seq(pkt) == recv_seq) {//如果序号和recv_seq一致，则接受
                    int n = _unmake_pkt(&pkt, buff, PACKET_MAX_SIZE);
                    //printf("write_buffer %d %s %d\n", n, buff, getpid());
                    //char *p_buff = buff;
                    int offset = 0;
                    while(offset < n) {
                        offset += write_buffer(&recv_buff, buff + offset, n - offset);
                        pthread_cond_signal(&recv_buff.cond);
                    }
                    recv_seq = (recv_seq + 1) % 2;
                }
            }
        }
    }
}

static void _timeout(void *arg)
{
    struct Timeout_data *data = arg;
    //printf("%d retransmission\n", getpid());
    pthread_mutex_lock(&data->lock);
    write(data->fd, &data->pkt, PACKET_MAX_SIZE);
    data->tid = timer_start(500, data);
    pthread_mutex_unlock(&data->lock);
}