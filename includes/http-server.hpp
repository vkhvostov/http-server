#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef OS_LINUX
#include <sys/epoll.h>
#endif

#ifdef OS_MAC
#include <sys/event.h>
#endif

#include "network/Config.hpp"
#include "network/Socket.hpp"

#define LOG(msg) std::cout << "[http-server] " << msg << std::endl
#define LOG_ERR(msg) std::cerr << "[http-server ERROR] " << msg << std::endl

template <typename T>
inline std::string toString(T value) {
	std::ostringstream oss;
	oss << value;
	return oss.str();
}

#endif
