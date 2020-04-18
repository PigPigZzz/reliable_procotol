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
#define timer_start(ms, data) \
do\
{\
    pthread_mutex_lock(&data.lock);\
    data.tid = _timer(ms, _timeout, &data);\
    pthread_mutex_unlock(&data.lock);\
} while(0)
#define timer_stop(data) \
do\
{\
    pthread_mutex_lock(&data.lock);\
    pthread_cancel(data.tid);\
    pthread_mutex_unlock(&data.lock);\
} while(0)

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

//发送方的窗口数据
struct Window_data{
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int next_seq;
    int base;
    int nack_pkt_size;
    struct Packet pkts[WINDOW_SIZE];
};
//计时线程数据
struct Timeout_data{
    pthread_t tid;//计时线程
    int fd;//重传时的文件描述符
    pthread_mutex_t lock;
    struct Window_data *pkts_p;//重传包
};

static struct Window_data send_data = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 1, 1, 0};
static struct Timeout_data timeout_data = {-1, -1, PTHREAD_MUTEX_INITIALIZER, &send_data};
static struct Buffer send_buff, recv_buff;//接受，发送数据缓存
int except_seq = 1;//接收方期待的序号

static void _rdt_handle(int proc_fd, int conn_fd)
{
    fd_set template_set, read_set, write_set;//template_set用于方便加载模板
    struct timeval template_time, tv;
    pthread_t send_tid, recv_tid;
    char buff[BUFF_SIZE];
    //初始化缓冲区
    init_buffer(&send_buff, BUFF_SIZE);
    init_buffer(&recv_buff, BUFF_SIZE);
    //初始化计时线程的fd
    timeout_data.fd = conn_fd;
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
    for(;;){
        //printf("want to send %d\n", getpid());
        int n = read_buffer(&send_buff, buff, sizeof(buff));//从缓冲区读取数据，如果没有数据会被锁住
        //printf("read send_buff %d byte %s %d\n", n, buff, getpid());
        pthread_cond_signal(&send_buff.cond);//通知要写入缓冲区但没有空间的进程，可以写入数据了
        //发送窗口是否还有资源
        pthread_mutex_lock(&send_data.lock);
        while(send_data.nack_pkt_size == WINDOW_SIZE)
            pthread_cond_wait(&send_data.cond, &send_data.lock);
        //开始计时
        if(!send_data.nack_pkt_size)
            timer_start(500, timeout_data);
        index_pkt = send_data.next_seq % WINDOW_SIZE;//计算数据包放入索引
        send_data.pkts[index_pkt] = _make_pkt(send_data.next_seq, buff, 0, n);
        send_data.next_seq = (send_data.next_seq + 1) % MAX_SEQ;//增加序号
        send_data.nack_pkt_size++;//增加未确认包数量
        pthread_mutex_unlock(&send_data.lock);

        write(fd, &send_data.pkts[index_pkt], PACKET_MAX_SIZE);
        //printf("%d send lock\n", getpid());
    }
}

static void *_rdt_recv_thread(void *arg)
{
    int fd = *(int*)arg;
    struct Packet pkt;
    char buff[PACKET_MAX_SIZE];
    struct Packet ack_pkt = _make_pkt(0, NULL, 1, 0);
    for(;;){
        //printf("want to read %d\n", getpid());
        int n = read(fd, &pkt, PACKET_MAX_SIZE);
        //printf("read recv_buff %d byte %d\n", n, getpid());
        //printf("check %d isack:%d\n", _is_correct(pkt), _is_ack(pkt));
        if(!_is_correct(pkt)){
            if(_is_ack(pkt)){
                int base = (_get_seq(pkt) + 1) % MAX_SEQ;
                //取消计时器
                timer_stop(timeout_data);
                //查看确认包序号
                pthread_mutex_lock(&send_data.lock);
                send_data.nack_pkt_size -= (base - send_data.base + MAX_SEQ) % MAX_SEQ;//重新计算未确认包数量
                send_data.base = base;
                if(send_data.nack_pkt_size)//如果还有数据未确认，则启动计时器
                    timer_start(500, timeout_data);
                pthread_mutex_unlock(&send_data.lock);
                pthread_cond_signal(&send_data.cond);//通知发送线程空间可用
            } else{
                //printf("is data seq:%d %d\n", _get_seq(pkt), getpid());
                if(_get_seq(pkt) == except_seq) {//如果序号和except_seq一致，则接受
                    ack_pkt = _make_pkt(_get_seq(pkt), NULL, 1, 0);//回送一个ack
                    write(fd, &ack_pkt, PACKET_MAX_SIZE);
                    int n = _unmake_pkt(&pkt, buff, PACKET_MAX_SIZE);
                    //printf("write_buffer %d %s %d\n", n, buff, getpid());
                    //char *p_buff = buff;
                    int offset = 0;
                    while(offset < n) {
                        offset += write_buffer(&recv_buff, buff + offset, n - offset);
                        pthread_cond_signal(&recv_buff.cond);
                    }
                    except_seq = (except_seq + 1) % MAX_SEQ;
                } else{//不一致则发送之前的ack包
                    write(fd, &ack_pkt, PACKET_MAX_SIZE);
                }
            }//_is_ack
        }//_is_correct
    }//for
}

static void _timeout(void *arg)
{
    struct Timeout_data *time_data = arg;
    struct Window_data *pkts_p = time_data->pkts_p;
    //printf("%d retransmission\n", getpid());
    //上锁保护线程数据
    pthread_mutex_lock(&time_data->lock);
    //重发所有未确认数据
    pthread_mutex_lock(&pkts_p->lock);
    for(int i = 0; i < time_data->pkts_p->nack_pkt_size; i++) {
        struct Packet *pkt_p = pkts_p->pkts + ((pkts_p->base + i) % WINDOW_SIZE);
        write(time_data->fd, pkt_p, PACKET_MAX_SIZE);
    }
    pthread_mutex_unlock(&pkts_p->lock);
    pthread_mutex_unlock(&time_data->lock);
    timer_start(500, (*time_data));
}