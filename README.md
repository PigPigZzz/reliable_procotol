[TOC]
# 计算机网络：三种可靠的传输协议实现
## 1、代码结构
### 代码文件结构
1. 缓存工具文件：utils.h、utils.c&emsp;提供可靠的缓冲区读取、写入，以及缓冲区可读可写的信号传输
2. 协议工具文件：_rdt.h、_rdt.c&emsp;提供如打包pkt、校验码等函数、宏、结构体
3. 协议执行文件：rdt.h、rdt.c&emsp;&emsp;创建协议进程为处理原进程与目标进程协议交流
### 程序结构
<div class="mermaid">
graph LR
    subgraph 父子关系图
        main["main"]
        forward["forward"]
        client1["client1"]
        client1_protocol["client1_protocol"]
        client2["client2"]
        client2_protocol["client2_protocol"]

        main-->forward
        main-->client1
        main-->client2
        client1-->client1_protocol
        client2-->client2_protocol
    end
</div>
```mermaid
graph LR
    subgraph 进程通信数据流
        subgraph program1
            subgraph connect_program1
                conn_program1_send_conn["send data to connect"]
                conn_program1_recv_conn["recv data fromm connect"]
                conn_program1_send_host["send data to program"]
                conn_program1_recv_host["recv data from program"]
                conn_program1_handel["handel data(make pkt...)"]

                conn_program1_recv_conn-->conn_program1_handel
                conn_program1_handel-->conn_program1_send_conn
                conn_program1_handel-->conn_program1_send_host
                conn_program1_recv_host-->conn_program1_handel
            end
            subgraph main_program1
                main_program1_send["send data"]
                main_program1_recv["recv data"]
            end

            main_program1_send-->conn_program1_recv_host
            conn_program1_send_host-->main_program1_recv
        end
        subgraph forward
            forward_recv_program2["recv program2 data"]
            forward_send_program2["send data to program2 from program1"]
            forward_recv_program1["recv program1 data"]
            forward_send_program1["send data to program1 from program2"]
            forward_handel["interfere data(drop and interfere data)"]

            forward_recv_program1-->forward_handel
            forward_recv_program2-->forward_handel
            forward_handel-->forward_send_program1
            forward_handel-->forward_send_program2
        end
        subgraph program2
            subgraph main_program2
                main_program2_send["send data"]
                main_program2_recv["recv data"]
            end
            subgraph connect_program2
                conn_program2_send_host["send data to program"]
                conn_program2_recv_host["recv data from program"]
                conn_program2_handel["handel data(make pkt...)"]
                conn_program2_send_conn["send data to connect"]
                conn_program2_recv_conn["recv data fromm connect"]

                conn_program2_recv_conn-->conn_program2_handel
                conn_program2_handel-->conn_program2_send_conn
                conn_program2_handel-->conn_program2_send_host
                conn_program2_recv_host-->conn_program2_handel
            end

            main_program2_send-->conn_program2_recv_host
            conn_program2_send_host-->main_program2_recv
        end

        conn_program1_send_conn-->forward_recv_program1
        forward_send_program1-->conn_program1_recv_conn
        conn_program2_send_conn-->forward_recv_program2
        forward_send_program2-->conn_program2_recv_conn
    end
```

## 2、程序使用
1. 编译
进入对应协议文件夹
`make`
2. 执行
命令行文本回射
`./main`
传输文件
`./main < xxx > yyy`
3. 清理文件
`./make clean`

我的博客：https://blog.csdn.net/qq_33690566/article/details/105415681
