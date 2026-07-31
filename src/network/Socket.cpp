#include "network/Socket.hpp"
#include "http-server.hpp"

#include <cstring>
#include <stdexcept>

Socket::Socket() : fd(-1), host("0.0.0.0"), port(0) {
	std::memset(&addr, 0, sizeof(addr));
}

Socket::Socket(const std::string &host, int port) : fd(-1), host(host), port(port) {
	std::memset(&addr, 0, sizeof(addr));
}

Socket::~Socket() {
	close();
}

void Socket::create() {
	if (fd >= 0)
		throw std::runtime_error("Socket::create() called on already-open socket");
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		throw std::runtime_error("socket() failed: " + std::string(strerror(errno)));
}

void Socket::setReuseAddr() {
	if (fd < 0)
		throw std::runtime_error("Socket not created");
	int opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		throw std::runtime_error("setsockopt(SO_REUSEADDR) failed: " + std::string(strerror(errno)));
}

void Socket::setNonBlocking() {
	if (fd < 0)
		throw std::runtime_error("Socket not created");
	setNonBlocking(fd);
}


void Socket::bind() {
	if (fd < 0)
		throw std::runtime_error("Socket not created");
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (host.empty() || host == "0.0.0.0") {
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	} else {
		struct addrinfo hints, *res;
		std::memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		int status = getaddrinfo(host.c_str(), NULL, &hints, &res);
		if (status != 0)
			throw std::runtime_error("getaddrinfo failed for '" + host + "': " + std::string(gai_strerror(status)));

		addr.sin_addr = reinterpret_cast<struct sockaddr_in *>(res->ai_addr)->sin_addr;
		freeaddrinfo(res);
	}

	if (::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
		throw std::runtime_error("bind() failed on " + host + ":" + toString(port) + ": " + std::string(strerror(errno)));

	if (port == 0) {
		socklen_t len = sizeof(addr);
		if (getsockname(fd, reinterpret_cast<struct sockaddr *>(&addr), &len) < 0)
			throw std::runtime_error("getsockname() failed: " + std::string(strerror(errno)));
		port = ntohs(addr.sin_port);
	}
}

void Socket::listen(int backlog) {
	if (fd < 0)
		throw std::runtime_error("Socket not created");
	if (::listen(fd, backlog) < 0)
		throw std::runtime_error("listen() failed: " + std::string(strerror(errno)));
	LOG("Listening on " + host + ":" + toString(port));
}

int Socket::accept() {
	struct sockaddr_in clientAddr;
	socklen_t clientLen = sizeof(clientAddr);
	return ::accept(fd, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientLen);
}

void Socket::close() {
	if (fd >= 0) {
		::close(fd);
		fd = -1;
	}
}

int Socket::getFd() const {
	return fd;
}

int Socket::getPort() const {
	return port;
}

const std::string &Socket::getHost() const {
	return host;
}

bool Socket::isValid() const {
	return fd >= 0;
}

void Socket::setNonBlocking(int fd) {
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
		throw std::runtime_error("fcntl(F_SETFL, O_NONBLOCK) failed: " + std::string(strerror(errno)));
	// Close-on-exec keeps server fds out of forked CGI children.
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
		throw std::runtime_error("fcntl(F_SETFD, FD_CLOEXEC) failed: " + std::string(strerror(errno)));
}

