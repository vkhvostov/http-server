#ifndef EVENT_LOOP_HPP
#define EVENT_LOOP_HPP

#include "http/Request.hpp"
#include "http/RequestHandler.hpp"
#include "http/CGI.hpp"
#include "http-server.hpp"

#include <csignal>
#include <ctime>
#include <map>
#include <set>
#include <string>

class Config;
struct RouteConfig;

struct ClientConnection {
	int fd;
	std::string writeBuffer;
	size_t writeOffset; // bytes of writeBuffer already sent (avoids O(n^2) erase)
	Request request;
	bool responseReady;
	bool keepAlive;
	bool draining;
	std::time_t lastActivity;
	int serverPort;
	std::string clientIP;

	CGI *cgi;
	std::string cgiScriptPath;

	ClientConnection();
};

class EventLoop {
public:
	EventLoop();
	~EventLoop();

	void init();
	void addListenSocket(int fd, int port);

	void addClient(int fd, int serverPort);
	void removeClient(int fd);
	void setWriteReady(int fd);
	void setReadReady(int fd);

	void setConfig(const Config *cfg);

	void run();
	void stop();

	ClientConnection *getConnection(int fd);

private:
	static const int MAX_EVENTS = 64;
	static const int TIMEOUT_MS = 1000;
	static const int CLIENT_TIMEOUT = 60;

	int pollFd;
	bool running;

	const Config *config;
	RequestHandler handler;

	std::map<int, ClientConnection> connections;
	std::set<int> listenFds;
	std::map<int, int> listenPorts;
	std::map<int, int> cgiFdToClient;

#ifdef OS_MAC
	struct kevent events[MAX_EVENTS];
#endif
#ifdef OS_LINUX
	struct epoll_event events[MAX_EVENTS];
#endif

	void initPoll();
	void addToRead(int fd);
	void addToWrite(int fd);
	void removeFromPoll(int fd);
	void modifyToRead(int fd);
	void modifyToWrite(int fd);
	int waitForEvents();

	int getEventFd(int index);
	bool isReadEvent(int index);
	bool isWriteEvent(int index);
	bool isErrorEvent(int index);
	bool isEofEvent(int index);

	void handleNewConnection(int listenFd);
	void handleClientRead(int clientFd);
	void handleClientWrite(int clientFd);
	void handleCompleteRequest(int clientFd);
	void resetForKeepAlive(int clientFd);
	void checkTimeouts();

	// Routing helpers
	const ServerConfig *findMatchingServer(int serverPort) const;
	const RouteConfig *findMatchingRoute(const std::string &cleanPath, int serverPort) const;
	bool startCgiForConnection(ClientConnection &conn, const std::string &scriptPath,
							const RouteConfig *route);
	void handleCgiRead(int cgiFd);
	void handleCgiWrite(int cgiFd);
	void completeCgi(ClientConnection &conn);
	void abortCgi(ClientConnection &conn);
	bool isCgiFd(int fd) const;
	void stripBodyIfHead(ClientConnection &conn);

	EventLoop(const EventLoop &);
	EventLoop &operator=(const EventLoop &);
};

#endif
