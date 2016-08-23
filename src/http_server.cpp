/*
 * http_server.cpp
 *
 *  Created on: Oct 26, 2014
 *      Author: liao
 */

#include "http_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>
//#include <sys/socket.h>
//#include <sys/time.h>
//#include <sys/epoll.h>
//#include <sys/fcntl.h>
//#include <unistd.h>
#include <sstream>
#include "simple_log.h"
#include "sim_parser.h"

#include <memory>

HttpMethod::HttpMethod(int code, std::string name) {
    _codes.insert(code);
    _names.insert(name);
}

HttpMethod& HttpMethod::operator|(HttpMethod hm) {
    std::set<int> *codes = hm.get_codes();
    _codes.insert(codes->begin(), codes->end());
    std::set<std::string> *names = hm.get_names();
    _names.insert(names->begin(), names->end());
    return *this;
}

std::set<int> *HttpMethod::get_codes() {
    return &_codes;
}

std::set<std::string> *HttpMethod::get_names() {
    return &_names;
}

HttpServer::HttpServer() {
    _port = 3456; // default port 
    _backlog = 10;
    _max_events = 1000;
    _pid = 0;
}

int HttpServer::start(int port, int backlog, int max_events) {
    LOG_WARN("start() method is deprecated, please use start_sync() or start_async() instead!");
    // return iocp_socket.start_epoll(port, http_handler, backlog, max_events);
    return iocp_socket.start_iocp(port, http_handler, backlog);
}

void *http_start_routine(void *ptr) {
    HttpServer *hs = (HttpServer *) ptr;
    hs->start_sync();
    return NULL;
}

int HttpServer::start_async() {
    //int ret = pthread_create(&_pid, NULL, http_start_routine, this);
    DWORD tid;
    _pid = ::CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)http_start_routine, this, 0, &tid);
    //if (ret != 0) {
    if (NULL==_pid) {
        //LOG_ERROR("HttpServer::start_async err:%d", ret);
        DWORD ret = ::GetLastError();
        LOG_ERROR("HttpServer::start_async err:%d", ret);
        return ret;
    }
    return 0;
}

int HttpServer::join() {
    if (_pid == 0) {
        LOG_ERROR("HttpServer not start async!");
        return -1;
    }
    //return pthread_join(_pid, NULL);
    ::WaitForSingleObject(_pid, INFINITE);

    return 0;
}

int HttpServer::start_sync() {
    // return iocp_socket.start_epoll(_port, http_handler, _backlog, _max_events);
    return iocp_socket.start_iocp(_port, http_handler, _backlog);
}

void HttpServer::add_mapping(std::string path, method_handler_ptr handler, HttpMethod method) {
    http_handler.add_mapping(path, handler, method);
}

void HttpServer::add_mapping(std::string path, json_handler_ptr handler, HttpMethod method) {
    http_handler.add_mapping(path, handler, method);
}

void HttpServer::add_bind_ip(std::string ip) {
    iocp_socket.add_bind_ip(ip);
}

void HttpServer::set_thread_pool(ThreadPool *tp) {
    iocp_socket.set_thread_pool(tp);
}

void HttpServer::set_backlog(int backlog) {
    _backlog = backlog;
}

void HttpServer::set_max_events(int me) {
    _max_events = me;
}

void HttpServer::set_port(int port) {
    _port = port;
}

void HttpIOCPWatcher::add_mapping(std::string path, method_handler_ptr handler, HttpMethod method) {
    Resource resource = {method, handler, NULL};
    resource_map[path] = resource;
}

void HttpIOCPWatcher::add_mapping(std::string path, json_handler_ptr handler, HttpMethod method) {
    Resource resource = {method, NULL, handler};
    resource_map[path] = resource;
}

int HttpIOCPWatcher::handle_request(Request &req, Response &res) {
    std::string uri = req.get_request_uri();
    if (this->resource_map.find(uri) == this->resource_map.end()) { // not found
        res._code_msg = STATUS_NOT_FOUND;
        res._body = STATUS_NOT_FOUND.msg;
        LOG_INFO("page not found which uri:%s", uri.c_str());
        return 0;
    }

    Resource resource = this->resource_map[req.get_request_uri()];
    // check method
    HttpMethod method = resource.method;
    if (method.get_names()->count(req._line.get_method()) == 0) {
        res._code_msg = STATUS_METHOD_NOT_ALLOWED;
        std::string allow_name = *(method.get_names()->begin()); 
        res.set_head("Allow", allow_name);
        res._body.clear();
        LOG_INFO("not allow method, allowed:%s, request method:%s", 
            allow_name.c_str(), req._line.get_method().c_str());
        return 0;
    }

    if (resource.json_ptr != NULL) {
        Json::Value root;
        resource.json_ptr(req, root);
        res.set_body(root);
    } else if (resource.handler_ptr != NULL) {
        resource.handler_ptr(req, res);
    }
    LOG_DEBUG("handle response success which code:%d, msg:%s", 
        res._code_msg.status_code, res._code_msg.msg.c_str());
    return 0;
}

int HttpIOCPWatcher::on_accept(IOCPContext &iocp_context) {
    int conn_sock = iocp_context.fd;
    iocp_context.ptr = new HttpContext(conn_sock);
    return 0;
}

int HttpIOCPWatcher::on_readable(IOCPContext &iocp_context, LPPER_IO_DATA PerIoData) {
    // IOCPContext *iocp_context = (IOCPContext *) event.data.ptr;
    // int fd = iocp_context->fd;

    // int buffer_size = SS_READ_BUFFER_SIZE;
    // //char read_buffer[buffer_size];
    // char read_buffer[SS_READ_BUFFER_SIZE];
    // memset(read_buffer, 0, buffer_size);

    // int read_size = recv(fd, read_buffer, buffer_size, 0);
    // if (read_size == -1 && errno == EINTR) {
    //     return READ_CONTINUE; 
    // } 
    // if (read_size == -1 /* io err*/|| read_size == 0 /* close */) {
    //     return READ_CLOSE;
    // }
    char *read_buffer = PerIoData->databuff.buf;
    int read_size = PerIoData->data_len;

    LOG_DEBUG("read success which read size:%d", read_size);
    HttpContext *http_context = (HttpContext *) iocp_context.ptr;
    if (http_context->get_requset()._parse_part == PARSE_REQ_LINE) {
        http_context->record_start_time();
    }

    int ret = http_context->get_requset().parse_request(read_buffer, read_size);
    if (ret < 0) {
        return READ_CLOSE;
    }
    if (ret == NEED_MORE_STATUS) {
        return READ_CONTINUE;
    }
    if (ret == PARSE_LEN_REQUIRED) {
        http_context->get_res()._code_msg = STATUS_LENGTH_REQUIRED;
        http_context->get_res()._body = STATUS_LENGTH_REQUIRED.msg;
        return READ_OVER;
    } 

    this->handle_request(http_context->get_requset(), http_context->get_res());

    // ��ʼд
    on_writeable_1(iocp_context);

    return READ_OVER;
}
    
int HttpIOCPWatcher::on_writeable_1(IOCPContext &iocp_context) {

    // int fd = iocp_context.fd;
    HttpContext *hc = (HttpContext *) iocp_context.ptr;
    Response &res = hc->get_res();
    //bool is_keepalive = (strcasecmp(hc->get_requset().get_header("Connection").c_str(), "keep-alive") == 0);
    bool is_keepalive = (_stricmp(hc->get_requset().get_header("Connection").c_str(), "keep-alive") == 0);

    if (!res._is_writed) {
        std::string http_version = hc->get_requset()._line.get_http_version();
        res.gen_response(http_version, is_keepalive);
        res._is_writed = true;
    }

    // char buffer[SS_WRITE_BUFFER_SIZE];
    // //bzero(buffer, SS_WRITE_BUFFER_SIZE);
    // memset(buffer, 0, SS_WRITE_BUFFER_SIZE);
    // int read_size = 0;

    // 1. read some response bytes
    // int ret = res.readsome(buffer, SS_WRITE_BUFFER_SIZE, read_size);
    // 2. write bytes to socket
    // int nwrite = send(fd, buffer, read_size, 0);

    std::auto_ptr<PER_IO_DATA> new_PerIoData(new PER_IO_DATA());
    // new_PerIoData = (LPPER_IO_DATA)GlobalAlloc(GPTR, sizeof(PER_IO_DATA));  
    ZeroMemory(&(new_PerIoData -> overlapped), sizeof(WSAOVERLAPPED));
    new_PerIoData->databuff.buf = new_PerIoData->buffer;

    int read_size = 0;
    int ret = res.readsome(new_PerIoData->databuff.buf, SS_BUFFER_SIZE, read_size);
    new_PerIoData->databuff.len = read_size;
    // new_PerIoData->data_len = len-1;
    new_PerIoData->operationType = 2;    // write

    new_PerIoData->write_continue = (ret==1);

    DWORD Flags = 0;  
    WSASend(iocp_context.fd, &(new_PerIoData->databuff), 1, NULL, Flags, &(new_PerIoData->overlapped), NULL);
    new_PerIoData.release();

    return 0;
}

int HttpIOCPWatcher::on_writeable(IOCPContext &iocp_context, LPPER_IO_DATA PerIoData) {

    HttpContext *hc = (HttpContext *) iocp_context.ptr;
    Response &res = hc->get_res();

    int nwrite = PerIoData->data_len;

    int read_size = PerIoData->databuff.len;
    
    if (nwrite < 0) {
        perror("send fail!");
        return WRITE_CONN_CLOSE;
    }
    // 3. when not write all buffer, we will rollback write index
    if (nwrite < read_size) {
        res.rollback(read_size - nwrite);
    }
    LOG_DEBUG("send complete which write_num:%d, read_size:%d", nwrite, read_size);

    // if (ret == 1) {/* not send over*/
    if (PerIoData->write_continue) {
        LOG_DEBUG("has big response, we will send part first and send other part later ...");

        on_writeable_1(iocp_context);
        return WRITE_CONN_CONTINUE;
    }

    // if (ret == 0 && nwrite == read_size) {
    if (nwrite == read_size) {
        hc->print_access_log(iocp_context.client_ip);
    }

    bool is_keepalive = (_stricmp(hc->get_requset().get_header("Connection").c_str(), "keep-alive") == 0);
    if (is_keepalive && nwrite > 0) {
        hc->clear();
        return WRITE_CONN_ALIVE;
    }
    return WRITE_CONN_CLOSE;

    // int fd = iocp_context.fd;
    // HttpContext *hc = (HttpContext *) iocp_context.ptr;
    // Response &res = hc->get_res();
    // //bool is_keepalive = (strcasecmp(hc->get_requset().get_header("Connection").c_str(), "keep-alive") == 0);
    // bool is_keepalive = (_stricmp(hc->get_requset().get_header("Connection").c_str(), "keep-alive") == 0);

    // if (!res._is_writed) {
    //     std::string http_version = hc->get_requset()._line.get_http_version();
    //     res.gen_response(http_version, is_keepalive);
    //     res._is_writed = true;
    // }

    // char buffer[SS_WRITE_BUFFER_SIZE];
    // //bzero(buffer, SS_WRITE_BUFFER_SIZE);
    // memset(buffer, 0, SS_WRITE_BUFFER_SIZE);
    // int read_size = 0;

    // // 1. read some response bytes
    // int ret = res.readsome(buffer, SS_WRITE_BUFFER_SIZE, read_size);
    // // 2. write bytes to socket
    // int nwrite = send(fd, buffer, read_size, 0);
    // if (nwrite < 0) {
    //     perror("send fail!");
    //     return WRITE_CONN_CLOSE;
    // }
    // // 3. when not write all buffer, we will rollback write index
    // if (nwrite < read_size) {
    //     res.rollback(read_size - nwrite);
    // }
    // LOG_DEBUG("send complete which write_num:%d, read_size:%d", nwrite, read_size);

    // if (ret == 1) {/* not send over*/
    //     LOG_DEBUG("has big response, we will send part first and send other part later ...");
    //     return WRITE_CONN_CONTINUE;
    // }

    // if (ret == 0 && nwrite == read_size) {
    //     hc->print_access_log(iocp_context.client_ip);
    // }

    // if (is_keepalive && nwrite > 0) {
    //     hc->clear();
    //     return WRITE_CONN_ALIVE;
    // }
    // return WRITE_CONN_CLOSE;
}

int HttpIOCPWatcher::on_close(IOCPContext &iocp_context) {
    if (iocp_context.ptr == NULL) {
        return 0;
    }
    HttpContext *hc = (HttpContext *) iocp_context.ptr;
    if (hc != NULL) {
        delete hc;
        hc = NULL;
    }
    return 0;
}
