CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: main.cpp ./http/http_conn.cpp ./mysql_pool/sql_pool.cpp webserver.cpp config.cpp 
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient
clean:
	rm  -r server
