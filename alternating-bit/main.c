#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/socket.h>
#include "rdt.h"

void switchboard(void);
int forward(int read_fd, int write_fd);
void host1(void);
void host2(void);

int main(void)
{
    pid_t switch_pid, host1_pid, host2_pid;
    switch_pid = fork();
    if(switch_pid == 0){
        //printf("switch %d\n", getpid());
        switchboard();
        _exit(0);
    }
    sleep(1);
    host1_pid = fork();
    if(host1_pid == 0){
        //printf("host1 %d\n", getpid());
        host1();
        _exit(0);
    }
    host2_pid = fork();
    if(host2_pid == 0){
        //printf("host2 %d\n", getpid());
        host2();
        _exit(0);
    }
    waitpid(switch_pid, NULL, 0);
    waitpid(host1_pid, NULL, 0);
    waitpid(host2_pid, NULL, 0);
    return 0;
}

void switchboard(void)
{
    int host1_fd, host2_fd, listen_fd;
    int max_fd;
    fd_set read_set;
    unlink("/tmp/socket_listen");
    listen_fd = init_conn(INIT_SWITCH, "/tmp/socket_listen");
    host1_fd = accept(listen_fd, NULL, NULL);
    host2_fd = accept(listen_fd, NULL, NULL);
    max_fd = host1_fd > host2_fd ? host1_fd : host2_fd;
    for(;;){
        FD_ZERO(&read_set);
        FD_SET(host1_fd, &read_set);
        FD_SET(host2_fd, &read_set);
        //printf("switch select\n");
        select(max_fd + 1, &read_set, NULL, NULL, NULL);
        if(FD_ISSET(host1_fd, &read_set)) {
            //printf("host1 host2 ");
            forward(host1_fd, host2_fd);
        }
        if(FD_ISSET(host2_fd, &read_set)) {
            //printf("host2 host1 ");
            forward(host2_fd, host1_fd);
        }
    }
}

int forward(int read_fd, int write_fd)
{
    struct Packet pkt;
    char *buf = (char*)&pkt;
    int n;
    n = read(read_fd, buf, PACKET_MAX_SIZE);
    //printf("seq %d isack %d ", _get_seq(pkt), _is_ack(pkt));
    if(rand() %100 <= 5) {
        //printf("discard\n");
        return 0;
    }
    if(rand() % 100 <= 5) {
        buf[1] = (char)rand();
        //printf("interfere ");
    }
    //printf("forward %d\n", n);
    if(write(write_fd, buf, n) != n)
        return -1;
    return 0;
}

void host1(void)
{
    int fd = init_conn(INIT_HOST, "/tmp/socket_listen");
    char buff[1024];
    int count = 0;
    for(;;){
        int n = read(STDIN_FILENO, buff, sizeof(buff));
        if(n <= 0)
            pause();
        write(fd, buff, n);
        /*sprintf(buff, "%d hello world", count);
        write(fd, buff, strlen(buff));
        printf("host1 sendnum %d\n", count++);
        int n = read(fd, buff, 1024);
        buff[n] = 0;
        printf("host1 recv %s %d\n", buff, n);*/
        //sleep(2);
    }
}

void host2(void)
{
    int fd = init_conn(INIT_HOST, "/tmp/socket_listen");
    char buff[1024];
    int count = 0;
    for(;;){
        int n = read(fd, buff, sizeof(buff));
        write(STDOUT_FILENO, buff, n);
        /*sprintf(buff, "%d hello world", count);
        write(fd, buff, strlen(buff));
        printf("host2 sendnum %d\n", count++);
        //sleep(10);
        int n = read(fd, buff, 1024);
        buff[n] = 0;
        printf("host2 recv %s %d\n", buff, n);*/
        //sleep(2);
    }
}
