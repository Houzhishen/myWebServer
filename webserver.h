#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./http/http_conn.h"
#include "./mysql_pool/sql_pool.h"

const int MAX_FD = 65536;
const int MAX_EVENT_NUM = 10000;

class WebServer {
public:
    WebServer();
    ~WebServer();

    void init(int port, string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);
    void sql_pool();
    void enventListen();
    void enventLoop();
    bool dealClientData();

public:
    int m_port;
    char* m_root;
    http_conn* users;
    int m_listenfd;
    int m_close_log;

    connection_pool* m_connPool;
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;
};



#endif