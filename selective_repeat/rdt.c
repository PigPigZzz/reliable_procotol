//
// Created by xjs on 2020/3/29.
//

#include "rdt.h"
#include "utils.h"
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>

#define WINDOW_SIZE 8   //窗口大小
#define MAX_SEQ 16      //最大序号
#define timer_start(ms, data) data.t_data.tid = _timer(ms, _timeout, &data)
#define timer_stop(data) pthread_cancel(data.t_data.tid)

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

//计时线程数据
struct Timeout_data{
    pthread_t tid;//计时线程
    int fd;//重传时的文件描述符
    //struct Window_data *pkts_p;//重传包
};

//窗口数据结构
struct Window_unit{
    struct Timeout_data t_data;
    struct Packet pkt;
    pthread_mutex_t lock;
    int flag;
};
struct Window{
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int base;
    struct Window_unit pkts[WINDOW_SIZE];
};

static struct Window send_window = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
static struct Window recv_window = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
static struct Buffer send_buff, recv_buff;//接受，发送数据缓存

static void _rdt_handle(int proc_fd, int conn_fd)
{
    fd_set template_set, read_set, write_set;//template_set用于方便加载模板
    struct timeval template_time, tv;
    pthread_t send_tid, recv_tid;
    char buff[BUFF_SIZE];
    //初始化缓冲区
    init_buffer(&send_buff, BUFF_SIZE);
    init_buffer(&recv_buff, BUFF_SIZE);
    //创建发送、接受线程
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
    destory_buffer(&send_buff);
    destory_buffer(&recv_buff);
}

static void *_rdt_send_thread(void *arg)
{
    int fd = *(int*)arg;
    char buff[PACKET_DATA_SIZE];
    int index_pkt;
    for(int i = 0; i < WINDOW_SIZE; i++){//初始化窗口数据单元
        send_window.pkts[i].flag = 0;
        send_window.pkts[i].t_data.fd = fd;
        pthread_mutex_init(&send_window.pkts[i].lock, NULL);
    }
    for(;;){
        //printf("want to send %d\n", getpid());
        index_pkt = send_window.base % WINDOW_SIZE;//计算数据包放入索引
        int n = read_buffer(&send_buff, buff, sizeof(buff));//从缓冲区读取数据，如果没有数据会被锁住
        //printf("read send_buff %d byte %s %d\n", n, buff, getpid());
        pthread_cond_signal(&send_buff.cond);//通知要写入缓冲区但没有空间的线程，可以写入数据了
        pthread_mutex_lock(&send_window.lock);
        //发送窗口是否还有资源
        while(send_window.pkts[index_pkt].flag)
            pthread_cond_wait(&send_window.cond, &send_window.lock);
        pthread_mutex_lock(&send_window.pkts[index_pkt].lock);
        //开始计时
        timer_start(500, send_window.pkts[index_pkt]);
        send_window.pkts[index_pkt].pkt = _make_pkt(send_window.base, buff, 0, n);
        send_window.pkts[index_pkt].flag = 1;
        pthread_mutex_unlock(&send_window.pkts[index_pkt].lock);
        pthread_mutex_unlock(&send_window.lock);

        write(fd, &send_window.pkts[index_pkt].pkt, PACKET_MAX_SIZE);
        send_window.base = (send_window.base + 1) % MAX_SEQ;//增加序号
        //printf("%d send lock\n", getpid());
    }
}

static void *_rdt_recv_thread(void *arg)
{
    int fd = *(int*)arg;
    struct Packet pkt;
    int index_pkt;
    char buff[PACKET_MAX_SIZE];
    for(int i = 0; i < WINDOW_SIZE; i++){//初始化窗口数据单元
        recv_window.pkts[i].flag = 0;
        pthread_mutex_init(&recv_window.pkts[i].lock, NULL);
    }
    for(;;){
        //printf("want to read %d\n", getpid());
        int n = read(fd, &pkt, PACKET_MAX_SIZE);
        //printf("read recv_buff %d byte %d\n", n, getpid());
        //printf("check %d isack:%d\n", _is_correct(pkt), _is_ack(pkt));
        if(n == PACKET_MAX_SIZE && !_is_correct(pkt)){
            if(_is_ack(pkt)){
                int ack_seq = _get_seq(pkt);
                index_pkt = ack_seq % WINDOW_SIZE;
                pthread_mutex_lock(&send_window.lock);
                pthread_mutex_lock(&send_window.pkts[index_pkt].lock);
                //取消计时器
                timer_stop(send_window.pkts[index_pkt]);
                send_window.pkts[index_pkt].flag = 0;
                pthread_mutex_unlock(&send_window.pkts[index_pkt].lock);
                pthread_mutex_unlock(&send_window.lock);
                pthread_cond_signal(&send_window.cond);//通知发送线程空间可用
            } else{
                //printf("is data seq:%d\n", _get_seq(pkt));
                struct Packet ack_pkt = _make_pkt(_get_seq(pkt), NULL, 1, 0);//回送一个ack
                write(fd, &ack_pkt, PACKET_MAX_SIZE);
                int base = _get_seq(pkt);
                pthread_mutex_lock(&recv_window.lock);//上锁接受窗口
                if((base - recv_window.base + MAX_SEQ) % MAX_SEQ < WINDOW_SIZE) {//如果序号和在接受窗口之内，则接受
                    index_pkt = _get_seq(pkt) % WINDOW_SIZE;
                    pthread_mutex_lock(&recv_window.pkts[index_pkt].lock);
                    recv_window.pkts[index_pkt].pkt = pkt;
                    recv_window.pkts[index_pkt].flag = 1;
                    pthread_mutex_unlock(&recv_window.pkts[index_pkt].lock);
                    if(recv_window.base == _get_seq(pkt)) {
                        do {
                            index_pkt = recv_window.base % WINDOW_SIZE;
                            pthread_mutex_lock(&recv_window.pkts[index_pkt].lock);
                            int inner_n = _unmake_pkt(&recv_window.pkts[index_pkt].pkt, buff, PACKET_MAX_SIZE);
                            int offset = 0;
                            while(offset < inner_n) {
                                offset += write_buffer(&recv_buff, buff + offset, inner_n - offset);
                                pthread_cond_signal(&recv_buff.cond);
                            }
                            recv_window.pkts[index_pkt].flag = 0;
                            pthread_mutex_unlock(&recv_window.pkts[index_pkt].lock);
                        } while(recv_window.pkts[++recv_window.base % WINDOW_SIZE].flag);
                        recv_window.base %= MAX_SEQ;
                    }
                    //printf("write_buffer %d %s %d\n", inner_n, buff, getpid());
                    //char *p_buff = buff;
                }
                pthread_mutex_unlock(&recv_window.lock);//解锁接受窗口
            }//_is_ack
        }//_is_correct
    }//for
}

static void _timeout(void *arg)
{
    struct Window_unit *pkt_p = (struct Window_unit *)arg;
    //printf("%d retransmission\n", getpid());
    //重发未确认数据
    pthread_mutex_lock(&pkt_p->lock);
    write(pkt_p->t_data.fd, &pkt_p->pkt, PACKET_MAX_SIZE);
    timer_start(500, (*pkt_p));
    pthread_mutex_unlock(&pkt_p->lock);
}