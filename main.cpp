#include "config.h"

int main() {
    string user = "root";
    string passwd = "hzs";
    string databasename = "hzs";

    Config config;
    WebServer server;

    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
    server.sql_pool();
    server.enventListen();
    server.enventLoop();

    return 0;
}