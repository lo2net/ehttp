/*
 * iocp_socket.cpp
 *
 *  Created on: Nov 10, 2014
 *      Author: liao
 */
#include <cstdlib>
#include <climits>
#include <cstdio>
#include <cerrno>
//#include <netinet/in.h>
//#include <arpa/inet.h>
//#include <sys/socket.h>
//#include <sys/time.h>
//#include <sys/epoll.h>
//#include <sys/fcntl.h>
//#include <sys/sysinfo.h>
//#include <unistd.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "kernel32.lib")

#include <memory> // auto_ptr
#include "iocp_socket.h"

#include "simple_log.h"

static std::string MyGetLastErrorMsg(DWORD iErrCode)
{
    LPVOID lpMsgBuf = NULL;
    int len = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|
                            FORMAT_MESSAGE_FROM_SYSTEM|
                            FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL, iErrCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                            (LPSTR)&lpMsgBuf, 0, NULL);
    if (0==len || NULL==lpMsgBuf)
    {
        DWORD dwErr = ::GetLastError();
        char buf[MAX_PATH] = {0};
        sprintf(buf, "%d", dwErr);
        return buf;
    }
    std::string msg((char*)lpMsgBuf, len);
    ::LocalFree(lpMsgBuf);
    return msg;
}

IOCPSocket::IOCPSocket() {
    _thread_pool = NULL;
    _use_default_tp = true;

    m_completionPort = NULL;
    m_ghSemaphore = NULL;

    watcher = NULL;

    ::InitializeCriticalSection(&sockets_handle_data_cs);
}

IOCPSocket::~IOCPSocket() {
    if (_thread_pool != NULL && _use_default_tp) {
        delete _thread_pool;
        _thread_pool = NULL;
    }

    std::set<int>::iterator it = _listen_sockets.begin();
    for (; it!=_listen_sockets.end(); ++it)
    {
        closesocket(*it);
    }

    ::EnterCriticalSection(&sockets_handle_data_cs);
    std::list<LPPER_HANDLE_DATA>::iterator it_data = sockets_handle_data.begin();
    for (; it_data!=sockets_handle_data.end(); ++it_data)
    {
        close_and_release_helper(*it_data);
    }
    sockets_handle_data.clear();
    ::LeaveCriticalSection(&sockets_handle_data_cs);

    ::DeleteCriticalSection(&sockets_handle_data_cs);

    CloseHandle(m_completionPort);
    CloseHandle(m_ghSemaphore);

    WSACleanup();
}

// int IOCPSocket::setNonblocking(int fd) {
//     int flags;

//     if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
//         flags = 0;
//     return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
// };

int IOCPSocket::bind_on(unsigned int ip) {
    /* listen on sock_fd, new connection on new_fd */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET) {
        LOG_ERROR("socket error:%s", MyGetLastErrorMsg(::WSAGetLastError()).c_str());
        return -1;
    }
    // int opt = 1;
    BOOL opt = TRUE;
    if (SOCKET_ERROR==setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)))
    {
        LOG_ERROR("setsockopt error:%s", MyGetLastErrorMsg(::WSAGetLastError()).c_str());
        closesocket(sockfd);
        return -1;
    }

    // LPPER_HANDLE_DATA PerHandleData = (LPPER_HANDLE_DATA)GlobalAlloc(GPTR, sizeof(PER_HANDLE_DATA));  // 在堆中为这个PerHandleData申请指定大小的内存  
    std::auto_ptr<PER_HANDLE_DATA> PerHandleData(new PER_HANDLE_DATA()); // 在堆中为这个PerHandleData申请指定大小的内存
    PerHandleData->socket = sockfd;
    if (NULL==CreateIoCompletionPort((HANDLE)sockfd, m_completionPort, (ULONG_PTR)PerHandleData.get(), 0))
    {
        LOG_ERROR("associate iocp fail:%s", MyGetLastErrorMsg(::GetLastError()).c_str());
        closesocket(sockfd);
        return -1;
    }

    struct sockaddr_in my_addr; /* my address information */
    memset (&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET; /* host byte order */
    my_addr.sin_port = htons(_port); /* short, network byte order */
    my_addr.sin_addr.s_addr = ip;

    if (SOCKET_ERROR == bind(sockfd, (struct sockaddr *) &my_addr, sizeof(struct sockaddr))) {
        LOG_ERROR("bind error:%s", MyGetLastErrorMsg(::WSAGetLastError()).c_str());
        closesocket(sockfd);
        return -1;
    }
    if (SOCKET_ERROR == listen(sockfd, _backlog)) {
        LOG_ERROR("listen error:%s", MyGetLastErrorMsg(::WSAGetLastError()).c_str());
        // GlobalFree(PerHandleData);
        closesocket(sockfd);
        return -1;
    }
    _listen_sockets.insert(sockfd);

    ::EnterCriticalSection(&sockets_handle_data_cs);
    sockets_handle_data.push_back(PerHandleData.release());
    ::LeaveCriticalSection(&sockets_handle_data_cs);
    
    return 0;
}

int IOCPSocket::listen_on() {
    if (_bind_ips.empty()) {
        int ret = bind_on(INADDR_ANY); /* auto-fill with my IP */
        if (ret != 0) {
            return ret;
        }
        LOG_INFO("bind for all ip (0.0.0.0)!");
    } else {
        for (size_t i = 0; i < _bind_ips.size(); i++) {
            unsigned ip = inet_addr(_bind_ips[i].c_str());
            int ret = bind_on(ip);
            if (ret != 0) {
                return ret;
            }
            LOG_INFO("bind for ip:%s success", _bind_ips[i].c_str());
        }
    }

    LOG_INFO("start Server Socket on port : %d", _port);
    return 0;
}

// int IOCPSocket::accept_socket(int sockfd, std::string &client_ip) {
//     int new_fd;
//     struct sockaddr_in their_addr; /* connector's address information */
//     socklen_t sin_size = sizeof(struct sockaddr_in);

//     if ((new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size)) == -1) {
//         LOG_ERROR("accept error:%s", strerror(errno));
//         return -1;
//     }

//     client_ip = inet_ntoa(their_addr.sin_addr);
//     LOG_DEBUG("server: got connection from %s\n", client_ip.c_str());
//     return new_fd;
// }

int IOCPSocket::handle_accept_event(LPPER_HANDLE_DATA PerHandleData, LPPER_IO_DATA PerIoData, IOCPSocketWatcher &socket_handler) {

    // 释放信号量,以便继续accept
    ::ReleaseSemaphore(m_ghSemaphore, 1, NULL);

    // LPPER_IO_DATA PerIoData = event.m_per_io_data;
    // LPPER_HANDLE_DATA PerHandleData = event.m_per_handle_data;
    std::auto_ptr<PER_IO_DATA> tmp_PerIoData(PerIoData);
    
    //PER_HANDLE_DATA * new_PerHandleData = NULL;  
    SOCKADDR_IN saRemote;
    int RemoteLen;
            
    SOCKADDR_IN saLocal;

    SOCKET acceptSocket;  

    RemoteLen = sizeof(saRemote);  

    acceptSocket = tmp_PerIoData->accept_socket;

    SOCKET listen_socket = PerHandleData->socket;

    memset(&saRemote, 0, sizeof(SOCKADDR_IN));
    memset(&saLocal, 0, sizeof(SOCKADDR_IN));

    //*
    if (SOCKET_ERROR == setsockopt(acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&listen_socket, sizeof(listen_socket)))
    {
        LOG_ERROR("setsockopt() failed. Error: %s\n", MyGetLastErrorMsg(::GetLastError()).c_str());
        closesocket(acceptSocket);
        return -1;
    }

    int len = sizeof(sockaddr);
    getsockname(acceptSocket, (sockaddr*)&saLocal, &len);
    getpeername(acceptSocket, (sockaddr*)&saRemote, &len);


    LOG_DEBUG("get accept socket which listen fd:%d, conn_sock_fd:%d", listen_socket, acceptSocket);

    // 回调
    std::auto_ptr<IOCPContext> iocp_context(new IOCPContext());
    iocp_context->fd = acceptSocket;
    iocp_context->client_ip = inet_ntoa(saRemote.sin_addr);
    socket_handler.on_accept(*iocp_context);

    //*/

    /*
    // GetAcceptExSockaddrs
    LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs;
    GUID GuidAcceptEx = WSAID_GETACCEPTEXSOCKADDRS;
    DWORD dwBytes=0;
    WSAIoctl(acceptSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx, sizeof(GuidAcceptEx), &lpfnGetAcceptExSockaddrs, sizeof(lpfnGetAcceptExSockaddrs), &dwBytes, NULL, NULL);

    char lpOutputBuf[1024];
    int outBufLen = 1024;
    LPSOCKADDR pLocalAddr, pRemoteAddr;
    int local_len;
    int remote_len;
    lpfnGetAcceptExSockaddrs(PerIoData->buffer, 1024-(sizeof(sockaddr_in)+16)*2,
    sizeof(sockaddr_in)+16, sizeof(sockaddr_in)+16,
    (SOCKADDR**)&pLocalAddr, &local_len,
    (SOCKADDR**)&pRemoteAddr, &remote_len);
    memcpy(&saLocal, pLocalAddr, sizeof(saLocal));
    memcpy(&saRemote, pRemoteAddr, sizeof(saRemote));
    //*/
              
    // 创建用来和套接字关联的单句柄数据信息结构  
    // new_PerHandleData = (LPPER_HANDLE_DATA)GlobalAlloc(GPTR, sizeof(PER_HANDLE_DATA));  // 在堆中为这个PerHandleData申请指定大小的内存  
    std::auto_ptr<PER_HANDLE_DATA> new_PerHandleData(new PER_HANDLE_DATA());  // 在堆中为这个PerHandleData申请指定大小的内存  
    new_PerHandleData -> socket = acceptSocket;
    memcpy (&new_PerHandleData->ClientAddr, &saRemote, RemoteLen);  
    new_PerHandleData->iocp_context = iocp_context.release();  

    // 将接受套接字和完成端口关联  
    if (NULL==CreateIoCompletionPort((HANDLE)(new_PerHandleData->socket), m_completionPort, (DWORD)new_PerHandleData.get(), 0))
    {
        LOG_ERROR("associate iocp fail:%s", MyGetLastErrorMsg(::GetLastError()).c_str());
        closesocket(acceptSocket);
    }

    // 开始在接受套接字上处理I/O使用重叠I/O机制  
    // 在新建的套接字上投递一个或多个异步  
    // WSARecv或WSASend请求，这些I/O请求完成后，工作者线程会为I/O请求提供服务      
    // 单I/O操作数据(I/O重叠)

    // 准备读取io data
    std::auto_ptr<PER_IO_DATA> new_PerIoData(new PER_IO_DATA());
    ZeroMemory(&(new_PerIoData->overlapped), sizeof(WSAOVERLAPPED)); // 清空内存  
    new_PerIoData->databuff.len = SS_BUFFER_SIZE;  
    new_PerIoData->databuff.buf = new_PerIoData->buffer;  
    new_PerIoData->operationType = 1;    // read
    DWORD RecvBytes;  
    DWORD Flags = 0;  
    WSARecv(new_PerHandleData->socket, &(new_PerIoData->databuff), 1, NULL, &Flags, &(new_PerIoData->overlapped), NULL);
    new_PerIoData.release();

    // 保存起来后续释放
    ::EnterCriticalSection(&sockets_handle_data_cs);
    sockets_handle_data.push_back(new_PerHandleData.release());
    ::LeaveCriticalSection(&sockets_handle_data_cs);
    
    return 0;

    /*
    int sockfd = event.data.fd;

    std::string client_ip;
    int conn_sock = accept_socket(sockfd, client_ip);
    if (conn_sock == -1) {
        return -1;
    }
    setNonblocking(conn_sock);
    LOG_DEBUG("get accept socket which listen fd:%d, conn_sock_fd:%d", sockfd, conn_sock);

    IOCPContext *iocp_context = new IOCPContext();
    iocp_context->fd = conn_sock;
    iocp_context->client_ip = client_ip;

    socket_handler.on_accept(*iocp_context);

    struct epoll_event conn_sock_ev;
    conn_sock_ev.events = EPOLLIN;
    conn_sock_ev.data.ptr = iocp_context;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &conn_sock_ev) == -1) {
        LOG_ERROR("epoll_ctl: conn_sock:%s", strerror(errno));
        return -1;
    }

    return 0;
    //*/
}

void read_func(void *data) {
    std::auto_ptr<TaskData> tdata((TaskData *)data);
    //int epollfd = tdata->epollfd;
    //epoll_event event = (tdata->event);
    IOCPSocketWatcher &socket_handler = *(tdata->watcher);
    IOCPSocket *pThis = tdata->pThis;
    LPPER_HANDLE_DATA PerHandleData = tdata->PerHandleData;
    LPPER_IO_DATA PerIoData = tdata->PerIoData;

    //IOCPContext *iocp_context = (IOCPContext *) event.data.ptr;
    //int fd = iocp_context->fd;

    int ret = socket_handler.on_readable(*(PerHandleData->iocp_context), PerIoData);
    if (ret == READ_CLOSE) {
        pThis->close_and_release(PerHandleData);
        return;
    }
    if (ret == READ_CONTINUE) {
        // event.events = EPOLLIN;
        // epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

        // 继续读取
        // 为下一个重叠调用建立单I/O操作数据
        std::auto_ptr<PER_IO_DATA> new_PerIoData(new PER_IO_DATA());
        ZeroMemory(&(new_PerIoData->overlapped), sizeof(WSAOVERLAPPED)); // 清空内存  
        new_PerIoData->databuff.len = SS_BUFFER_SIZE;  
        new_PerIoData->databuff.buf = new_PerIoData->buffer;  
        new_PerIoData->operationType = 1;    // read
        DWORD RecvBytes;  
        DWORD Flags = 0;  
        WSARecv(PerHandleData->socket, &(new_PerIoData->databuff), 1, NULL, &Flags, &(new_PerIoData->overlapped), NULL);
        new_PerIoData.release();
    } else if (ret == READ_OVER) { // READ_OVER
        // event.events = EPOLLOUT;
        // epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    } else {
        LOG_ERROR("unkonw ret!");
    }
}

int IOCPSocket::handle_readable_event(LPPER_HANDLE_DATA PerHandleData, LPPER_IO_DATA PerIoData, IOCPSocketWatcher &socket_handler) {

    // LPPER_IO_DATA PerIoData = event.m_per_io_data;
    // LPPER_HANDLE_DATA PerHandleData = event.m_per_handle_data;

    // 开始数据处理，接收来自客户端的数据  
    //WaitForSingleObject(hMutex,INFINITE);

    //cout << "A Client(" << inet_ntoa(((sockaddr_in*)&PerHandleData->ClientAddr)->sin_addr) <<") says: " << PerIoData->databuff.buf << endl;  
    //ReleaseMutex(hMutex);  
      
    // 为下一个重叠调用建立单I/O操作数据
    // new_PerIoData = (LPPER_IO_DATA)GlobalAlloc(GPTR, sizeof(PER_IO_DATA));  
    // std::auto_ptr<PER_IO_DATA> new_PerIoData(new PER_IO_DATA());
    // ZeroMemory(&(new_PerIoData->overlapped), sizeof(WSAOVERLAPPED)); // 清空内存  
    // new_PerIoData->databuff.len = SS_BUFFER_SIZE;  
    // new_PerIoData->databuff.buf = new_PerIoData->buffer;  
    // new_PerIoData->operationType = 1;    // read
    // DWORD RecvBytes;  
    // DWORD Flags = 0;  
    // WSARecv(PerHandleData->socket, &(new_PerIoData->databuff), 1, &RecvBytes, &Flags, &(new_PerIoData->overlapped), NULL);
    // new_PerIoData.release();

    // IOCPContext *iocp_context = (IOCPContext *) event.data.ptr;
    // int fd = iocp_context->fd;
    // epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &event); // TODO remove event or use EPOLLET?

    std::auto_ptr<PER_IO_DATA> auto_PerIoData(PerIoData);
    std::auto_ptr<TaskData> tdata(new TaskData());
    // tdata->epollfd = epollfd;
    // tdata->event = event;
    tdata->PerHandleData = PerHandleData;
    tdata->PerIoData = auto_PerIoData.release();
    tdata->watcher = &socket_handler;
    tdata->pThis = this;
    
    //*
    std::auto_ptr<Task> task(new Task(read_func, tdata.release()));
    int ret = _thread_pool->add_task(task.release());
    if (ret != 0) {
        LOG_WARN("add read task fail:%d, we will close connect.", ret);
        close_and_release(PerHandleData);
        // GlobalFree(new_PerIoData);
    }
    return ret;
    //*/
    /*
    read_func(tdata.release());
    return 0;
    //*/
}

void write_func(void *data) {
    std::auto_ptr<TaskData> tdata((TaskData *)data);
    // int epollfd = tdata->epollfd;
    // epoll_event event = (tdata->event);
    IOCPSocketWatcher &socket_handler = *(tdata->watcher);
    IOCPSocket *pThis = tdata->pThis;
    LPPER_HANDLE_DATA PerHandleData = tdata->PerHandleData;
    LPPER_IO_DATA PerIoData = tdata->PerIoData;

    // IOCPContext *iocp_context = (IOCPContext *) event.data.ptr;
    IOCPContext *iocp_context = PerHandleData->iocp_context;
    
    int fd = iocp_context->fd;
    LOG_DEBUG("start write data");

    int ret = socket_handler.on_writeable(*iocp_context, PerIoData);
    if(ret == WRITE_CONN_CLOSE) {
        pThis->close_and_release(PerHandleData);
        //delete tdata;
        return;
    }

    if (ret == WRITE_CONN_CONTINUE)
    {
        return;
    }

    // WRITE_CONN_ALIVE
    // 继续下个读请求
    std::auto_ptr<PER_IO_DATA> new_PerIoData(new PER_IO_DATA());
    ZeroMemory(&(new_PerIoData->overlapped), sizeof(WSAOVERLAPPED)); // 清空内存  
    new_PerIoData->databuff.len = SS_BUFFER_SIZE;  
    new_PerIoData->databuff.buf = new_PerIoData->buffer;  
    new_PerIoData->operationType = 1;    // read
    DWORD RecvBytes;  
    DWORD Flags = 0;  
    WSARecv(PerHandleData->socket, &(new_PerIoData->databuff), 1, NULL, &Flags, &(new_PerIoData->overlapped), NULL);
    new_PerIoData.release();

    // if (ret == WRITE_CONN_CONTINUE) {
    //     event.events = EPOLLOUT;
    // } else {
    //     event.events = EPOLLIN;
    // }
    // epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    // delete PerIoData;
    // delete tdata;
}

int IOCPSocket::handle_writeable_event(LPPER_HANDLE_DATA PerHandleData, LPPER_IO_DATA PerIoData, IOCPSocketWatcher &socket_handler) {
    // IOCPContext *iocp_context = (IOCPContext *) event.data.ptr;
    IOCPContext *iocp_context = (IOCPContext *) PerHandleData->iocp_context;
    
    // int fd = iocp_context->fd;
    // epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &event);

    std::auto_ptr<PER_IO_DATA> auto_PerIoData(PerIoData);
    std::auto_ptr<TaskData> tdata(new TaskData());
    // tdata->epollfd = epollfd;
    // tdata->event = event;
    tdata->PerHandleData = PerHandleData;
    tdata->PerIoData = auto_PerIoData.release();
    tdata->watcher = &socket_handler;
    tdata->pThis = this;

    //*
    std::auto_ptr<Task> task(new Task(write_func, tdata.release()));
    int ret = _thread_pool->add_task(task.release());
    if (ret != 0) {
        LOG_WARN("add write task fail:%d, we will close connect.", ret);
        close_and_release(PerHandleData);
    }
    return ret;
    //*/
    /*
    write_func(tdata.release());
    return 0;
    //*/
}

void IOCPSocket::set_thread_pool(ThreadPool *tp) {
    this->_thread_pool = tp;
    _use_default_tp = false;
}

DWORD WINAPI ServerWorkThread(LPVOID lpParam)  
{
    IOCPSocket *pThis = (IOCPSocket*)lpParam;
    // HANDLE CompletionPort = pThis->m_completionPort;

    IOCPSocketWatcher &socket_handler = *(pThis->watcher);

    // int epollfd = CompletionPort;
    
    DWORD BytesTransferred = 0;  
    LPWSAOVERLAPPED IpOverlapped = NULL;  
    LPPER_HANDLE_DATA PerHandleData = NULL;  
    //std::auto_ptr<PER_IO_DATA> PerIoData;
    DWORD RecvBytes = 0;  
    DWORD Flags = 0;  
    BOOL bRet = false;  

    while(true){  
        bRet = GetQueuedCompletionStatus(pThis->m_completionPort, &BytesTransferred, (PULONG_PTR)&PerHandleData, (LPWSAOVERLAPPED*)&IpOverlapped, INFINITE);
        std::auto_ptr<PER_IO_DATA> PerIoData((LPPER_IO_DATA)CONTAINING_RECORD(IpOverlapped, PER_IO_DATA, overlapped));
        if(bRet == 0){  
            LOG_ERROR("GetQueuedCompletionStatus Error: %s\n", MyGetLastErrorMsg(::GetLastError()).c_str());

            // if (NULL!=PerHandleData)
            // {
            //     closesocket(PerHandleData->socket);  
            //     // GlobalFree(PerHandleData);
            //     delete PerHandleData; PerHandleData = NULL;
            // }
            // GlobalFree(PerIoData);
            // delete PerIoData; PerIoData = NULL;

            return -1;  
        }  

        // 保存实际接收到的数据大小
        PerIoData->data_len = BytesTransferred;

        // 检查在套接字上是否有错误发生  
        // if(0 == BytesTransferred){
        //     if (NULL!=PerHandleData)
        //     {
        //         closesocket(PerHandleData->socket);  
        //         GlobalFree(PerHandleData);
        //     }
        //     GlobalFree(PerIoData);
        //     continue;  
        // }

        //epoll_event event;
        //event.m_per_handle_data = PerHandleData;
        //event.m_per_io_data = PerIoData;
        
        if (PerIoData->operationType == 0) // connect
        {
            // this->handle_accept_event(epollfd, event, socket_handler);
            pThis->handle_accept_event(PerHandleData, PerIoData.release(), socket_handler);
        }
        else if (PerIoData->operationType == 1) // read
        {
            if (0==BytesTransferred)
            {
                pThis->close_and_release(PerHandleData);
                continue;
            }
            // this->handle_readable_event(epollfd, event, socket_handler);
            pThis->handle_readable_event(PerHandleData, PerIoData.release(), socket_handler);
        }
        else if (PerIoData->operationType == 2) // write
        {
            // this->handle_writeable_event(epollfd, event, socket_handler);
            pThis->handle_writeable_event(PerHandleData, PerIoData.release(), socket_handler);
        }
        else
        {
            // unknown
        }
    }  
    return 0;

    // epoll_event *events = new epoll_event[max_events];
    // while (1) {
    //     int fds_num = epoll_wait(epollfd, events, max_events, -1);
    //     if (fds_num == -1) {
    //         if (errno == EINTR) { /*The call was interrupted by a signal handler*/
    //             continue;
    //         }
    //         LOG_ERROR("epoll_pwait:%s", strerror(errno));
    //         break;
    //     }

    //     for (int i = 0; i < fds_num; i++) {
    //         if(_listen_sockets.count(events[i].data.fd)) {
    //             // accept connection
    //             this->handle_accept_event(epollfd, events[i], socket_handler);
    //         } else if(events[i].events & EPOLLIN ){
    //             // readable
    //             this->handle_readable_event(epollfd, events[i], socket_handler);
    //         } else if(events[i].events & EPOLLOUT) {
    //             // writeable
    //             this->handle_writeable_event(epollfd, events[i], socket_handler);
    //         } else {
    //             LOG_INFO("unkonw events :%d", events[i].events);
    //         }
    //     }
    // }
    // if (events != NULL) {
    //     delete[] events;
    //     events = NULL;
    // }
    // return -1;
}

int IOCPSocket::start_iocp(int port, IOCPSocketWatcher &socket_handler, int backlog) {

    watcher = &socket_handler;

    // 加载socket动态链接库  
    WORD wVersionRequested = MAKEWORD(2, 2); // 请求2.2版本的WinSock库  
    WSADATA wsaData;    // 接收Windows Socket的结构信息  
    DWORD err = WSAStartup(wVersionRequested, &wsaData);  
    if (0 != err){  // 检查套接字库是否申请成功  
        LOG_ERROR("Request Windows Socket Library Error!\n");
        return -1;  
    }  
    if(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2){// 检查是否申请了所需版本的套接字库  
        LOG_ERROR("Request Windows Socket Version 2.2 Error!\n");
        return -1;  
    }  

    _backlog = backlog;
    _port = port;

    if (_thread_pool == NULL) {
        //int core_size = get_nprocs();
        SYSTEM_INFO mySysInfo;  
        GetSystemInfo(&mySysInfo);  
        int core_size = mySysInfo.dwNumberOfProcessors;

        LOG_INFO("thread pool not set, we will build for core size:%d", core_size);
        _thread_pool = new ThreadPool();
        _thread_pool->set_pool_size(core_size);
    }
    int ret = _thread_pool->start();
    if (ret != 0) {
        LOG_ERROR("thread pool start error:%d", ret);
        return ret;
    }

    m_completionPort = ::CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 0);  
    if (NULL == m_completionPort){    // 创建IO内核对象失败
        LOG_ERROR("CreateIoCompletionPort failed. Error: %s\n", MyGetLastErrorMsg(::GetLastError()).c_str());
        return -1;
    }

    ret = this->listen_on();
    if (ret != 0) {
        return ret;
    }

    // The "size" parameter is a hint specifying the number of file
    // descriptors to be associated with the new instance.
    /*
    int epollfd = epoll_create(1024);
    if (epollfd == -1) {
        LOG_ERROR("epoll_create:%s", strerror(errno));
        return -1;
    }
    //*/
    
    /*
    for (std::set<int>::iterator i = _listen_sockets.begin(); i != _listen_sockets.end(); i++) {
        int sockfd = *i;
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = sockfd;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
            LOG_ERROR("epoll_ctl: listen_sock:%s", strerror(errno));
            return -1;
        }
    }
    //*/
    // 确定处理器的核心数量  
    SYSTEM_INFO mySysInfo;  
    GetSystemInfo(&mySysInfo);  

    // 基于处理器的核心数量创建线程  
    for(DWORD i = 0; i < (mySysInfo.dwNumberOfProcessors * 2); ++i){  
        // 创建服务器工作器线程，并将完成端口传递到该线程  
        HANDLE ThreadHandle = CreateThread(NULL, 0, ServerWorkThread, this, 0, NULL);  
        if(NULL == ThreadHandle){  
            LOG_ERROR("Create Thread Handle failed. Error: %s\n", MyGetLastErrorMsg(::GetLastError()).c_str());
            return -1;  
        }  
        CloseHandle(ThreadHandle);  
    }  

    // AcceptEx
    LPFN_ACCEPTEX lpfnAcceptEx;
    if (!_listen_sockets.empty())
    {
        GUID GuidAcceptEx = WSAID_ACCEPTEX;
        DWORD dwBytes=0;
        WSAIoctl(*_listen_sockets.begin(), SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx, sizeof(GuidAcceptEx), &lpfnAcceptEx, sizeof(lpfnAcceptEx),
                 &dwBytes, NULL, NULL);
    }
    
    m_ghSemaphore = ::CreateSemaphore(NULL, MAX_PARALLEL_CONNECTION_ACCEPT, MAX_PARALLEL_CONNECTION_ACCEPT, NULL);
    if (NULL==m_ghSemaphore)
    {
        LOG_ERROR("::CreateSemaphore() failed. Error: %s\n", MyGetLastErrorMsg(::GetLastError()).c_str());
        return -1;
    }
    while(true)
    {
        ::WaitForSingleObject(m_ghSemaphore, INFINITE);

        for (std::set<int>::iterator i = _listen_sockets.begin(); i != _listen_sockets.end(); i++) {
            int srvSocket = *i;

            SOCKET accept_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (INVALID_SOCKET==accept_socket)
            {
                LOG_ERROR("create accept socket failed. Error: %s\n", MyGetLastErrorMsg(::GetLastError()).c_str());
                return -1;
            }

            // PerIoData = (LPPER_IO_DATA)GlobalAlloc(GPTR, sizeof(PER_IO_DATA));  
            std::auto_ptr<PER_IO_DATA> PerIoData(new PER_IO_DATA());
            ZeroMemory(&(PerIoData -> overlapped), sizeof(WSAOVERLAPPED));  
            PerIoData->databuff.len = SS_BUFFER_SIZE;  
            PerIoData->databuff.buf = PerIoData->buffer;  
            PerIoData->operationType = 0;    // connect
            PerIoData->accept_socket = accept_socket;

            DWORD dwBytes=0;
            //char lpOutputBuf[1024];
            //int outBufLen = 1024;
            //lpfnAcceptEx(srvSocket, accept_socket, lpOutputBuf, outBufLen-((sizeof(sockaddr_in)+16)*2),
            //lpfnAcceptEx(srvSocket, accept_socket, PerIoData->buffer, 1024-((sizeof(sockaddr_in)+16)*2),
            BOOL ret = lpfnAcceptEx(srvSocket, accept_socket, PerIoData->buffer, 0, // not wait for any data
                                    sizeof(sockaddr_in)+16,
                                    sizeof(sockaddr_in)+16, &dwBytes, &PerIoData->overlapped);
            // if (!ret) // fail
            // {
            //     LOG_ERROR("AcceptEx() failed. Error: %s\n", MyGetLastErrorMsg(::WSAGetLastError()).c_str());
            //     return -1;
            // }
            PerIoData.release();
        }
    }

    //never ends here
    return 0;
}

int IOCPSocket::close_and_release_helper(LPPER_HANDLE_DATA PerHandleData) {
    LOG_DEBUG("connect close");
    // IOCPContext *hc = (IOCPContext *) epoll_event.data.ptr;

    IOCPContext *hc = (IOCPContext*)PerHandleData->iocp_context;
    watcher->on_close(*hc);
    delete PerHandleData->iocp_context;
    PerHandleData->iocp_context = NULL;

    // int fd = hc->fd;
    // epoll_event.events = EPOLLIN | EPOLLOUT;
    // epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &epoll_event);

    // delete (IOCPContext *) epoll_event.data.ptr;
    // epoll_event.data.ptr = NULL;

    int fd = PerHandleData->socket;
    int ret = closesocket(fd);
    LOG_DEBUG("connect close complete which fd:%d, ret:%d", fd, ret);

    delete PerHandleData;

    return 0;
}

int IOCPSocket::close_and_release(LPPER_HANDLE_DATA PerHandleData) {
    // if (epoll_event.data.ptr == NULL) {
    //     return 0;
    // }
    ::EnterCriticalSection(&sockets_handle_data_cs);
    std::list<LPPER_HANDLE_DATA>::iterator it = sockets_handle_data.begin();
    for (; it!=sockets_handle_data.end();)
    {
        if (*it == PerHandleData)
        {
            close_and_release_helper(*it);
            sockets_handle_data.erase(it++);
        }
        else
        {
            ++it;
        }
    }
    ::LeaveCriticalSection(&sockets_handle_data_cs);

    return 0;
}

void IOCPSocket::add_bind_ip(std::string ip) {
    _bind_ips.push_back(ip);
}
