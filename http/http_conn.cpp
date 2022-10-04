#include "http_conn.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

map<string, string> users;

void http_conn::initmysql(connection_pool* conn_pool) {
    MYSQL* mysql = NULL;
    connectionRAII mysqlconn(&mysql, conn_pool);
    m_connPool = conn_pool;

    if (mysql_query(mysql, "select username, passwd from user")) {
        printf("select error\n");
    }

    cout << "mysql: " << mysql << endl;

    // 检索完整结果集
    MYSQL_RES* result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;

    }

}

int http_conn::m_user_count = 0;

void http_conn::init() {
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_len = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    // timer_flag = 0;
    // improv = 0;

    mysql = mysql_init(mysql);
    mysql = mysql_real_connect(mysql, "localhost", sql_user, sql_passwd, sql_name, 3306, NULL, 0);

    memset(m_read_buffer, '\0', READ_BUFFER_SIZE);
    memset(m_write_buffer, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

void http_conn::init(int sockfd, const sockaddr_in &addr, char* root, int TrihMode, 
                     int close_log, string user, string passwd, string sqlname) {

    m_sockfd = sockfd;
    m_address = addr;

    doc_root = root;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());
    m_user_count++;

    init();
}

bool http_conn::read_once() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;
    bytes_read = recv(m_sockfd, m_read_buffer + m_read_idx, READ_BUFFER_SIZE-m_read_idx, 0);
    printf("Recv: %s\n", m_read_buffer + m_read_idx);
    m_read_idx += bytes_read;
    if (bytes_read <= 0) {
        return false;
    }
    return true;

}

bool http_conn::write() {

    int temp = 0;

    if (bytes_to_send == 0) {
        init();
        return true;
    }

    while (true) {
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0) {
            if (errno == EAGAIN) {
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buffer + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0) {
            unmap();
            if (m_linger) {
                init();
                return true;
            }
            return false;
        }
    }
    // strcat(m_write_buffer, "HTTP/1.1 200 OK\r\n"); 
    // strcat(m_write_buffer, "Connection: close\r\n"); 
    // strcat(m_write_buffer, "\r\n"); 

    // char* file = (char*)malloc(sizeof(char)*200);
    // strcpy(file, "./root/welcome.html");
    // stat(file, &m_file_stat);
    // char* file_addr;

    // int fd = open(file, O_RDONLY);
    // file_addr = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    // close(fd);
    // strcat(m_write_buffer, file_addr);

    // send(m_sockfd, m_write_buffer, WRITE_BUFFER_SIZE-1, 0);
}


//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; m_checked_idx++) {
        temp = m_read_buffer[m_checked_idx];
        if (temp == '\r') {
            if (m_checked_idx+1 == m_read_idx) {
                return LINE_OPEN;
            } else if (m_read_buffer[m_checked_idx+1] == '\n') {
                m_read_buffer[m_checked_idx++] = '\0';
                m_read_buffer[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_idx>1 && m_read_buffer[m_checked_idx-1] == '\r') {
                m_read_buffer[m_checked_idx-1] = '\0';
                m_read_buffer[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    printf("text: %s\n", text);
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }

    *m_url++ = '\0';
    char* method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    printf("url_now: %s\n", m_url);

    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    if (strlen(m_url) == 1) {
        strcat(m_url, "0");
    }

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;

}

//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    // 判断是否为空行
    printf("parse header:\n");
    printf("m_check_state: %d\n", m_check_state);
    printf("m_content_len: %d\n", m_content_len);
    printf("text: %s\n", text);
    if (text[0] == '\0') {
        if (m_content_len != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_len = atoi(text);
        printf("m_content_len______: %d\n", m_content_len);
    } else if (strncasecmp(text, "Host", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } 

    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    printf("m_read_idx: %d, m_content_len: %d, m_checked_idx: %d\n", m_read_idx, m_content_len, m_checked_idx);
    if (m_read_idx >= m_content_len + m_checked_idx) {
        text[m_content_len] = '\0';
        m_string = text;
        printf("m_string____: %s\n", m_string);
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || (parse_line() == LINE_OK)) {
        printf("====\n");
        printf("cur_state: %d\n", m_check_state);

        text = get_line();
        m_start_line = m_checked_idx;
        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                printf("%d\n", ret);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                printf("parse content: %d", ret);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }

        
            default:
                return INTERNAL_ERROR;
        }
    }

    return NO_REQUEST;
}

bool http_conn::process_write(HTTP_CODE ret) {
    printf("write ret: %d\n", ret);
    switch (ret) {
    case INTERNAL_ERROR: {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST: {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST: {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST: {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0) {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buffer;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        } else {
            const char* ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string)) {
                return false;
            }
        }
        break;
    }
        
    default:
        return false;
    }

    m_iv[0].iov_base = m_write_buffer;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char* p = strrchr(m_url, '/');
    printf("doc_root: %s\n", doc_root);
    printf("request url: %s\n", m_url);
    printf("cgi: %d\n", cgi);
    // 处理 cgi
    if (cgi == 1 && (*(p+1) == '2' || *(p+1) == '3')) {
        // 判断注册 or 登录
        char flag = m_url[1];
        printf("flag: %d\n", flag);

        char* m_url_real = (char*) malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcpy(m_url_real, m_url+2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        printf("m_string: %s\n", m_string);

        // 提取密码
        char name[100], passwd[100];
        int i;
        for (i=5; m_string[i] != '&'; i++) {
            name[i-5] = m_string[i];
        }
        name[i-5] = '\0';

        int j=0;
        for (i=i+10; m_string[i] != '\0'; i++, j++) {
            passwd[j] = m_string[i];
        }
        passwd[j] = '\0';

        printf("name: %s, passwd: %s\n", name, passwd);
        // 如果是注册
        if (*(p+1) == '3') {
            // 检测数据库中是否有重名，没有则增加数据
            char* sql_insert = (char*)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            printf("sql_insert: %s\n", sql_insert);
            strcat(sql_insert, "'");
            printf("sql_insert: %s\n", sql_insert);
            strcat(sql_insert, name);
            printf("sql_insert: %s\n", sql_insert);
            strcat(sql_insert, "', '");
            printf("sql_insert: %s\n", sql_insert);
            strcat(sql_insert, passwd);
            printf("sql_insert: %s\n", sql_insert);
            strcat(sql_insert, "')");

            printf("sql_insert: %s\n", sql_insert);
            cout << "mysql__: " << mysql << endl;
            // connectionRAII mysqlconn(&mysql, m_connPool);
            // cout << "mysql__: " << mysql << endl;
            if (users.find(name) == users.end()) {
                int res = mysql_query(mysql, sql_insert);
                printf("res: %d\n", res);
                users.insert(pair<string, string>(name, passwd));
                if (!res) {
                    strcpy(m_url, "/log.html");
                } else {
                    strcpy(m_url, "/registerError.html");
                }
            } else {
                strcpy(m_url, "/registerError.html");
            }
            printf("m_url: %s\n", m_url);

        } else if (*(p+1) == '2') {
            // 登录，判断是否有该用户
            if (users.find(name) != users.end()) {
                strcpy(m_url, "/welcome.html");
            } else {
                strcpy(m_url, "/logError.html");
            }
        }
    }

    if (*(p+1) == '0') {
        char* m_url_real = (char*) malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        printf("m_real_file: %s\n", m_real_file);

        free(m_url_real);
    } else {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
        printf("m_real_file_000: %s\n", m_real_file);
    }

    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }

    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}


void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }

}



bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buffer + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    return true;
}

bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}

bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content) {
    return add_response("%s", content);
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        printf("Close connect %d\n", m_sockfd);
        // removefd()
        m_sockfd = -1;
        m_user_count--;
    }
}

