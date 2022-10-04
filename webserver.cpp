#include "webserver.h"

WebServer::WebServer() {
    users = new http_conn[MAX_FD];
    char server_path[200];
    getcwd(server_path, 200);

    char root[6] = "/root";
    m_root = (char*) malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);
}

WebServer::~WebServer() {
    close(m_listenfd);
    delete [] users;
}


void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model) {
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_close_log = close_log;
}

void WebServer::sql_pool() {
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    users->initmysql(m_connPool);
}

void WebServer::enventListen() {
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);
 
    printf("建立socket描述符");

    int ret = 0;
    struct sockaddr_in sockaddr;
    bzero(&sockaddr, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    sockaddr.sin_port = htons(m_port);

    int flag = 1;
    ret = bind(m_listenfd, (struct sockaddr*) &sockaddr, sizeof(sockaddr));
    assert(ret >= 0);
    printf("绑定端口");
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);
    printf("开始监听");
    
}

bool WebServer::dealClientData() {
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
    if (connfd < 0) {
        return false;
    }

    users[connfd].init(connfd, client_address, m_root, 0, 0, m_user, m_passWord, m_databaseName);
    users[connfd].read_once();
    users[connfd].process();
    users[connfd].write();

    close(connfd);

    return true;

} 

void WebServer::enventLoop() {
    
    bool stop_server = false;
    while (!stop_server) {
        int sockfd = m_listenfd;
        bool flag = dealClientData();
        if (!flag) {
            break;
        }
    }
}