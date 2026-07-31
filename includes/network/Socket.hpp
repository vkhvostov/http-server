#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <string>
#include <sys/socket.h>
#include <netinet/in.h>

class Socket {
public:
	Socket();
	Socket(const std::string &host, int port);
	~Socket();

	void create();
	void bind();
	void listen(int backlog = SOMAXCONN);
	int accept();
	void close();

	void setNonBlocking();
	void setReuseAddr();

	int getFd() const;
	int getPort() const;
	const std::string &getHost() const;
	bool isValid() const;

	static void setNonBlocking(int fd);

private:
	int fd;
	std::string host;
	int port;
	struct sockaddr_in addr;

	Socket(const Socket &);
	Socket &operator=(const Socket &);
};

#endif
