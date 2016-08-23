/*
 * iocp_socket.h
 *
 *  Created on: Nov 10, 2014
 *      Author: liao
 */

#ifndef _IOCP_SOCKET_H_
#define _IOCP_SOCKET_H_

#include <WinSock2.h>  
#include <Windows.h>  
#include "mswsock.h"

//#include "sys/epoll.h"
#include <vector>
#include <set>
#include <list>
#include <string>
#include "threadpool.h"


// #define SS_WRITE_BUFFER_SIZE 4096
// #define SS_READ_BUFFER_SIZE 4096
#define SS_BUFFER_SIZE 4096

#define WRITE_CONN_ALIVE 0
#define WRITE_CONN_CLOSE 1
#define WRITE_CONN_CONTINUE 2

#define READ_OVER 0
#define READ_CONTINUE 1
#define READ_CLOSE -1

#define MAX_PARALLEL_CONNECTION_ACCEPT 10

typedef struct  
{  
    WSAOVERLAPPED overlapped;    // overlapped要求第一个字段必须是此结构
    // 剩下字段自己定义
    // 自定义信息
    WSABUF databuff;             // socket收发缓冲区
    char buffer[SS_BUFFER_SIZE]; // 默认数据缓冲区长度
    int data_len;                // 实际有效数据长度

    int operationType;          // 当前操作类型

    SOCKET accept_socket;       // 保存第一次连接的socket,仅在处理accept的时候有用

    bool write_continue;        // 是否还需要继续写,仅在处理write的时候有用

}*LPPER_IO_DATA, PER_IO_DATA;

struct IOCPContext {
    void *ptr;
    int fd;
    std::string client_ip;
};

typedef struct  
{
    IOCPContext *iocp_context;

    SOCKET socket;  
    SOCKADDR_IN ClientAddr;
}PER_HANDLE_DATA, *LPPER_HANDLE_DATA;  

typedef void (*ScheduleHandlerPtr)();

class IOCPSocketWatcher {
    public:
        virtual int on_accept(IOCPContext &iocp_context) = 0;

    virtual int on_readable(IOCPContext &iocp_context, LPPER_IO_DATA PerIoData) = 0;

        /**
         * return :
         * if return value == 1, we will close the connection
         * if return value == 2, we will continue to write
         */
    virtual int on_writeable(IOCPContext &iocp_context, LPPER_IO_DATA PerIoData) = 0;

        virtual int on_close(IOCPContext &iocp_context) = 0;

};

class IOCPSocket;

struct TaskData {
    //int epollfd;
    //epoll_event event;
    LPPER_HANDLE_DATA PerHandleData;
    LPPER_IO_DATA PerIoData;
    IOCPSocketWatcher *watcher;
    IOCPSocket *pThis;
    ~TaskData() {
        delete PerIoData;
    }
};


class IOCPSocket {
    private:
    //int setNonblocking(int fd);

    //int accept_socket(int sockfd, std::string &client_ip);

        int bind_on(unsigned int ip);

        int listen_on();

public:
    
    int handle_accept_event(LPPER_HANDLE_DATA PerHandleData, LPPER_IO_DATA PerIoData, IOCPSocketWatcher &socket_watcher);

    int handle_readable_event(LPPER_HANDLE_DATA PerHandleData, LPPER_IO_DATA PerIoData, IOCPSocketWatcher &socket_watcher);

    int handle_writeable_event(LPPER_HANDLE_DATA PerHandleData, LPPER_IO_DATA PerIoData, IOCPSocketWatcher &socket_watcher);

    int close_and_release(LPPER_HANDLE_DATA PerHandleData);
    int close_and_release_helper(LPPER_HANDLE_DATA PerHandleData);

private:
        std::vector<std::string> _bind_ips;
        int _backlog;
        int _port;
        std::set<int> _listen_sockets;
    std::list<LPPER_HANDLE_DATA> sockets_handle_data;
    CRITICAL_SECTION sockets_handle_data_cs;
    
        ThreadPool *_thread_pool;
        bool _use_default_tp;

public:
        HANDLE m_completionPort;

    HANDLE m_ghSemaphore; // 可以connect服务器的socket预分配个数信号量

    IOCPSocketWatcher *watcher;

    public:
        IOCPSocket();
       
         ~IOCPSocket();

        int start_iocp(int port, IOCPSocketWatcher &socket_watcher, int backlog);

        void set_thread_pool(ThreadPool *tp);

        void set_schedule(ScheduleHandlerPtr h);

        void add_bind_ip(std::string ip);
};

#endif /* _IOCP_SOCKET_H_ */
