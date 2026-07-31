#include "network/EventLoop.hpp"
#include "network/Socket.hpp"
#include "network/Config.hpp"
#include "http/Response.hpp"
#include "http-server.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

ClientConnection::ClientConnection()
	: fd(-1),
	  writeOffset(0),
	  responseReady(false),
	  keepAlive(false),
	  draining(false),
	  lastActivity(std::time(NULL)),
	  serverPort(0),
	  cgi(NULL) {}

EventLoop::EventLoop() : pollFd(-1), running(false), config(NULL) {
	std::memset(events, 0, sizeof(events));
}

void EventLoop::setConfig(const Config *cfg) {
	config = cfg;
}

EventLoop::~EventLoop() {
	for (std::map<int, ClientConnection>::iterator it = connections.begin();
	     it != connections.end(); ++it) {
		if (it->second.cgi != NULL) {
			it->second.cgi->abort();
			delete it->second.cgi;
			it->second.cgi = NULL;
		}
		::close(it->first);
	}
	connections.clear();
	cgiFdToClient.clear();

	if (pollFd >= 0) {
		::close(pollFd);
		pollFd = -1;
	}
}

void EventLoop::init() {
	signal(SIGPIPE, SIG_IGN);
	initPoll();
}

void EventLoop::addListenSocket(int fd, int port) {
	listenFds.insert(fd);
	listenPorts[fd] = port;
	addToRead(fd);
	LOG("Registered listen socket fd=" + toString(fd) + " on port " + toString(port));
}

void EventLoop::addClient(int fd, int serverPort) {
	ClientConnection conn;
	conn.fd = fd;
	conn.serverPort = serverPort;
	conn.lastActivity = std::time(NULL);
	connections[fd] = conn;

	addToRead(fd);
}

void EventLoop::removeClient(int fd) {
	std::map<int, ClientConnection>::iterator it = connections.find(fd);
	if (it == connections.end())
		return;

	// Abort active CGI before closing the client connection.
	if (it->second.cgi != NULL)
		abortCgi(it->second);

	removeFromPoll(fd);
	::close(fd);
	connections.erase(it);
	LOG("Closed connection: fd=" + toString(fd));
}

bool EventLoop::isCgiFd(int fd) const {
	return cgiFdToClient.find(fd) != cgiFdToClient.end();
}

void EventLoop::setWriteReady(int fd) {
	modifyToWrite(fd);
}

void EventLoop::setReadReady(int fd) {
	modifyToRead(fd);
}

void EventLoop::stop() {
	running = false;
}

ClientConnection *EventLoop::getConnection(int fd) {
	std::map<int, ClientConnection>::iterator it = connections.find(fd);
	if (it == connections.end())
		return NULL;
	return &it->second;
}

void EventLoop::run() {
	running = true;
	while (running) {
		int nEvents = waitForEvents();
		if (nEvents < 0) {
			if (errno == EINTR) {
				continue;
			}
			throw std::runtime_error("waitForEvents() failed: " + std::string(strerror(errno)));
		}

		for (int i = 0; i < nEvents; ++i) {
			int fd = getEventFd(i);

			if (isErrorEvent(i)) {
				if (listenFds.count(fd))
					throw std::runtime_error("Error event on listen socket fd=" + toString(fd));
				if (isCgiFd(fd)) {
					std::map<int, int>::iterator it = cgiFdToClient.find(fd);
					if (it != cgiFdToClient.end()) {
						std::map<int, ClientConnection>::iterator cit = connections.find(it->second);
						if (cit != connections.end() && cit->second.cgi != NULL) {
							if (fd == cit->second.cgi->getReadFd())
								handleCgiRead(fd);
							else
								handleCgiWrite(fd);
						}
					}
					continue;
				}
				removeClient(fd);
				continue;
			}

			if (listenFds.count(fd) && isReadEvent(i)) {
				handleNewConnection(fd);
				continue;
			}

			// CGI pipe events are dispatched via cgiFdToClient mapping.
			if (isCgiFd(fd)) {
				if (isReadEvent(i))
					handleCgiRead(fd);
				if (isWriteEvent(i))
					handleCgiWrite(fd);
				if (!isReadEvent(i) && !isWriteEvent(i) && isEofEvent(i)) {
					std::map<int, int>::iterator it = cgiFdToClient.find(fd);
					if (it != cgiFdToClient.end()) {
						std::map<int, ClientConnection>::iterator cit = connections.find(it->second);
						if (cit != connections.end() && cit->second.cgi != NULL) {
							if (fd == cit->second.cgi->getReadFd())
								handleCgiRead(fd);
							else {
								removeFromPoll(fd);
								cgiFdToClient.erase(it);
							}
						}
					}
				}
				continue;
			}

			// On pure EOF/HUP, flush pending response before closing.
			if (isEofEvent(i) && !isReadEvent(i) && !isWriteEvent(i)) {
				if (connections.count(fd) &&
					!connections[fd].writeBuffer.empty())
					handleClientWrite(fd);
				if (connections.count(fd))
					removeClient(fd);
				continue;
			}

			if (isReadEvent(i) && connections.count(fd))
				handleClientRead(fd);

			if (isWriteEvent(i) && connections.count(fd))
				handleClientWrite(fd);
		}

		checkTimeouts();
	}
}

void EventLoop::handleNewConnection(int listenFd) {
	while (true) {
		struct sockaddr_in clientAddr;
		socklen_t addrLen = sizeof(clientAddr);
		int clientFd = ::accept(listenFd, reinterpret_cast<struct sockaddr *>(&clientAddr), &addrLen);
		if (clientFd < 0)
			break;

		try {
			Socket::setNonBlocking(clientFd);
		} catch (const std::exception &e) {
			LOG_ERR(std::string("Failed to configure client fd: ") + e.what());
			::close(clientFd);
			continue;
		}

		addClient(clientFd, listenPorts[listenFd]);
		// Dotted-quad formatted by hand; inet_ntoa is not in the allowed list.
		unsigned long ip = ntohl(clientAddr.sin_addr.s_addr);
		std::ostringstream ipStr;
		ipStr << ((ip >> 24) & 0xFF) << '.' << ((ip >> 16) & 0xFF) << '.'
		      << ((ip >> 8) & 0xFF) << '.' << (ip & 0xFF);
		connections[clientFd].clientIP = ipStr.str();
		if (config != NULL) {
			const std::vector<ServerConfig> &servers = config->getServers();
			for (size_t i = 0; i < servers.size(); ++i) {
				if (servers[i].port == listenPorts[listenFd]) {
					connections[clientFd].request.setMaxBodySize(
						servers[i].clientMaxBodySize);
					break;
				}
			}
		}
		LOG("New connection: fd=" + toString(clientFd) + " on port " + toString(listenPorts[listenFd]));
	}
}

void EventLoop::handleClientRead(int fd) {
	char buf[4096];
	ssize_t bytesRead = ::recv(fd, buf, sizeof(buf), 0);

	if (bytesRead == 0) {
		// Orderly shutdown; finish sending a pending response first.
		if (connections.count(fd)) {
			if (!connections[fd].writeBuffer.empty()) {
				connections[fd].keepAlive = false;
				return;
			}
			removeClient(fd);
		}
		return;
	}

	if (bytesRead < 0) {
		// No error event was flagged for this fd (the error dispatch runs
		// first and removes the client), so a negative return here is a
		// transient wakeup. errno checks after recv are forbidden; genuine
		// socket errors arrive as EPOLLERR/EPOLLHUP.
		return;
	}

	std::map<int, ClientConnection>::iterator it = connections.find(fd);
	if (it == connections.end())
		return;
	ClientConnection &conn = it->second;
	conn.lastActivity = std::time(NULL);

	// In drain mode, discard incoming bytes until peer closes.
	if (conn.draining)
		return;

	std::string chunk(buf, static_cast<size_t>(bytesRead));
	conn.request.parse(chunk);

	if (conn.request.getStatus() == Request::COMPLETE ||
		conn.request.getStatus() == Request::ERROR)
	{
		handleCompleteRequest(fd);
	}
}

void EventLoop::handleCompleteRequest(int fd) {
	ClientConnection &conn = connections[fd];

	// Handle CGI requests asynchronously.
	if (config != NULL && conn.request.getStatus() == Request::COMPLETE) {
		std::string cleanPath, queryStr;
		size_t qPos = conn.request.getPath().find('?');
		if (qPos != std::string::npos)
			cleanPath = conn.request.getPath().substr(0, qPos);
		else
			cleanPath = conn.request.getPath();
		const RouteConfig *matchedRoute = findMatchingRoute(cleanPath, conn.serverPort);

		if (matchedRoute != NULL && matchedRoute->maxBodySize > 0 &&
			conn.request.getBody().size() > matchedRoute->maxBodySize)
		{
			const ServerConfig *srv = findMatchingServer(conn.serverPort);
			Response res = handler.makeErrorResponse(413, "Request Entity Too Large", srv);
			conn.keepAlive = false;
			conn.draining = true;
			res.headers["Connection"] = "close";
			conn.writeBuffer = res.toString();
			conn.writeOffset = 0;
			conn.responseReady = true;
			modifyToWrite(fd);
			return;
		}

		// Check if GET is the only allowed method for this route
		bool getOnlyRoute = false;
		if (matchedRoute != NULL && matchedRoute->methods.size() == 1 &&
		    matchedRoute->methods[0] == "GET") {
			getOnlyRoute = true;
		}

		// CGI execution logic:
		// - If route has ONLY GET method: allow GET to run CGI
		// - If route has GET + other methods: run CGI only for non-GET methods
		bool shouldRunCgi = (conn.request.getMethod() != "GET") || getOnlyRoute;

		if (matchedRoute != NULL &&
			!matchedRoute->cgiExtension.empty() &&
			!matchedRoute->cgiPath.empty() &&
			cleanPath.size() >= matchedRoute->cgiExtension.size() &&
			cleanPath.substr(cleanPath.size() - matchedRoute->cgiExtension.size())
				== matchedRoute->cgiExtension &&
			shouldRunCgi)
		{
			if (cleanPath.find("..") != std::string::npos) {
				const ServerConfig *srv = findMatchingServer(conn.serverPort);
				Response res = handler.makeErrorResponse(403, "Forbidden", srv);
				std::string connHdr = conn.request.getHeader("connection");
				for (size_t i = 0; i < connHdr.size(); ++i)
					connHdr[i] = std::tolower(static_cast<unsigned char>(connHdr[i]));
				conn.keepAlive = (conn.request.getVersion() == "HTTP/1.1" && connHdr != "close");
				res.headers["Connection"] = conn.keepAlive ? "keep-alive" : "close";
				conn.writeBuffer = res.toString();
				conn.writeOffset = 0;
				conn.responseReady = true;
				modifyToWrite(fd);
				return;
			}
			std::string root = matchedRoute->root.empty()
							? config->getRoot()
							: matchedRoute->root;
			// Strip route prefix before joining with route root.
			std::string cgiSuffix = cleanPath;
			if (!matchedRoute->path.empty() && matchedRoute->path != "/") {
				if (cgiSuffix.size() >= matchedRoute->path.size() &&
				    cgiSuffix.substr(0, matchedRoute->path.size()) == matchedRoute->path)
					cgiSuffix = cgiSuffix.substr(matchedRoute->path.size());
				if (cgiSuffix.empty()) cgiSuffix = "/";
			}
			// Ensure "/" separator between root and suffix.
			std::string scriptPath = root;
			if (!root.empty() && root[root.size() - 1] != '/' && !cgiSuffix.empty() && cgiSuffix[0] != '/')
				scriptPath += "/";
			scriptPath += cgiSuffix;
			if (startCgiForConnection(conn, scriptPath, matchedRoute))
				return;
			// CGI start failed — return 500
			const ServerConfig *srv = findMatchingServer(conn.serverPort);
			Response res = handler.makeErrorResponse(500, "Internal Server Error", srv);
			std::string connHdr = conn.request.getHeader("connection");
			for (size_t i = 0; i < connHdr.size(); ++i)
				connHdr[i] = std::tolower(static_cast<unsigned char>(connHdr[i]));
			conn.keepAlive = (conn.request.getVersion() == "HTTP/1.1" && connHdr != "close");
			res.headers["Connection"] = conn.keepAlive ? "keep-alive" : "close";
			conn.writeBuffer = res.toString();
			conn.writeOffset = 0;
			stripBodyIfHead(conn);
			conn.responseReady = true;
			modifyToWrite(fd);
			return;
		}
	}

	// Non-CGI requests are handled synchronously.
	Response res;
	if (config != NULL) {
		std::string cleanPath;
		size_t qPos = conn.request.getPath().find('?');
		cleanPath = (qPos != std::string::npos)
			? conn.request.getPath().substr(0, qPos)
			: conn.request.getPath();
		const ServerConfig *srv = findMatchingServer(conn.serverPort);
		const RouteConfig *route = findMatchingRoute(cleanPath, conn.serverPort);
		if (srv != NULL)
			res = handler.handleRequest(conn.request, *srv, route);
		else
			res = handler.makeErrorResponse(500, "Internal Server Error");
	} else {
		res.statusCode = 500;
		res.statusMessage = "Internal Server Error";
		res.body = "Server is not configured.";
		res.headers["Content-Type"] = "text/plain";
	}

	std::string connHdr = conn.request.getHeader("connection");
	conn.keepAlive = (conn.request.getVersion() == "HTTP/1.1" && connHdr != "close");

	if (res.statusCode == 413) {
		conn.keepAlive = false;
		conn.draining = true;
	}
	// Error responses: always close so unread bodies don't corrupt the
	// next request's response stream on a keep-alive connection.
	if (res.statusCode >= 400)
		conn.keepAlive = false;
	res.headers["Connection"] = conn.keepAlive ? "keep-alive" : "close";

	conn.writeBuffer = res.toString();
	conn.writeOffset = 0;
	stripBodyIfHead(conn);
	conn.responseReady = true;
	modifyToWrite(fd);
}


// HEAD responses must have identical headers to GET but no body.
// Call this after conn.writeBuffer = res.toString() whenever the
// request method might be HEAD.
void EventLoop::stripBodyIfHead(ClientConnection &conn)
{
	if (conn.request.getMethod() != "HEAD")
		return;
	size_t sep = conn.writeBuffer.find("\r\n\r\n");
	if (sep != std::string::npos)
		conn.writeBuffer.resize(sep + 4);
}

const ServerConfig *EventLoop::findMatchingServer(int serverPort) const
{
	if (config == NULL) return NULL;
	const std::vector<ServerConfig> &servers = config->getServers();
	if (servers.empty()) return NULL;
	for (size_t i = 0; i < servers.size(); ++i) {
		if (servers[i].port == serverPort)
			return &servers[i];
	}
	return &servers[0];
}

const RouteConfig *EventLoop::findMatchingRoute(const std::string &cleanPath, int serverPort) const
{
	const ServerConfig *srv = findMatchingServer(serverPort);
	if (srv == NULL) return NULL;
	const std::vector<RouteConfig> &routes = srv->routes;
	const RouteConfig *best = NULL;
	for (size_t i = 0; i < routes.size(); ++i) {
		const std::string &rp = routes[i].path;
		if (cleanPath.substr(0, rp.size()) == rp) {
			// Enforce a path boundary after route prefix.
			if (cleanPath.size() > rp.size() &&
				rp[rp.size() - 1] != '/' &&
				cleanPath[rp.size()] != '/')
				continue;
			if (best == NULL || rp.size() > best->path.size())
				best = &routes[i];
		}
		// Also match route "/foo/" for request "/foo" — will trigger
		// a trailing-slash redirect in handleRequest.
		else if (!cleanPath.empty() && rp == cleanPath + "/") {
			if (best == NULL || rp.size() > best->path.size())
				best = &routes[i];
		}
	}
	return best;
}


bool EventLoop::startCgiForConnection(ClientConnection &conn,
									  const std::string &scriptPath,
									  const RouteConfig *route)
{
	conn.cgi = new CGI();
	conn.cgiScriptPath = scriptPath;

	std::string interpreter = (route != NULL && !route->cgiPath.empty())
							? route->cgiPath
							: "/usr/bin/python3";

	std::string serverName = "localhost";
	if (config != NULL) {
		const std::vector<ServerConfig> &servers = config->getServers();
		for (size_t i = 0; i < servers.size(); ++i) {
			if (servers[i].port == conn.serverPort) {
				if (!servers[i].serverName.empty())
					serverName = servers[i].serverName;
				break;
			}
		}
	}

	if (!conn.cgi->start(conn.request, scriptPath, interpreter,
						 conn.serverPort, serverName, conn.clientIP))
	{
		LOG_ERR("CGI start failed for " + scriptPath);
		delete conn.cgi;
		conn.cgi = NULL;
		return false;
	}

	// The CGI keeps its own copy of the body; free ours to halve the
	// memory held during large concurrent uploads.
	conn.request.releaseBody();

	// Register CGI pipe fds with the event loop.
	int wfd = conn.cgi->getWriteFd();
	int rfd = conn.cgi->getReadFd();
	if (wfd >= 0) {
		cgiFdToClient[wfd] = conn.fd;
		addToWrite(wfd);
	}
	if (rfd >= 0) {
		cgiFdToClient[rfd] = conn.fd;
		addToRead(rfd);
	}

	// Pause the client fd until the CGI finishes; a write registration here
	// would fire immediately and tear down the connection.
	removeFromPoll(conn.fd);
	return true;
}

void EventLoop::handleCgiWrite(int cgiFd)
{
	std::map<int, int>::iterator it = cgiFdToClient.find(cgiFd);
	if (it == cgiFdToClient.end()) return;
	std::map<int, ClientConnection>::iterator cit = connections.find(it->second);
	if (cit == connections.end()) return;
	ClientConnection &conn = cit->second;
	if (conn.cgi == NULL) return;

	bool more = conn.cgi->onWritable();
	if (!more) {
		removeFromPoll(cgiFd);
		cgiFdToClient.erase(cgiFd);
		close(cgiFd);
	}
}

void EventLoop::handleCgiRead(int cgiFd)
{
	std::map<int, int>::iterator it = cgiFdToClient.find(cgiFd);
	if (it == cgiFdToClient.end()) return;
	std::map<int, ClientConnection>::iterator cit = connections.find(it->second);
	if (cit == connections.end()) return;
	ClientConnection &conn = cit->second;
	if (conn.cgi == NULL) return;

	bool more = conn.cgi->onReadable();
	if (!more) {
		// CGI stdout reached EOF.
		removeFromPoll(cgiFd);
		cgiFdToClient.erase(cgiFd);
		completeCgi(conn);
	}
}

void EventLoop::completeCgi(ClientConnection &conn)
{
	// Ensure CGI write fd is unregistered.
	int wfd = conn.cgi->getWriteFd();
	if (wfd >= 0) {
		removeFromPoll(wfd);
		cgiFdToClient.erase(wfd);
	}

	conn.cgi->finish();

	const ServerConfig *srv = findMatchingServer(conn.serverPort);
	Response res;
	if (conn.cgi->hasTimedOut())
		res = handler.makeErrorResponse(504, "Gateway Timeout", srv);
	else if (conn.cgi->getExitStatus() != 0)
		res = handler.makeErrorResponse(500, "Internal Server Error", srv);
	else
		res = handler.parseCGIOutput(conn.cgi->getOutput());

	std::string connHdr = conn.request.getHeader("connection");
	for (size_t i = 0; i < connHdr.size(); ++i) {
		connHdr[i] = std::tolower(static_cast<unsigned char>(connHdr[i]));
	}
	bool wantsClose = (connHdr.find("close") != std::string::npos);
	conn.keepAlive = (conn.request.getVersion() == "HTTP/1.1" && !wantsClose);
	res.headers["Connection"] = conn.keepAlive ? "keep-alive" : "close";
	delete conn.cgi;
	conn.cgi = NULL;
	conn.writeBuffer = res.toString();
	conn.writeOffset = 0;
	conn.responseReady = true;
	// Client fd was removed from poll while CGI ran; re-register for write.
	addToWrite(conn.fd);
}

void EventLoop::abortCgi(ClientConnection &conn)
{
	if (conn.cgi == NULL) return;

	int wfd = conn.cgi->getWriteFd();
	int rfd = conn.cgi->getReadFd();
	if (wfd >= 0) { removeFromPoll(wfd); cgiFdToClient.erase(wfd); }
	if (rfd >= 0) { removeFromPoll(rfd); cgiFdToClient.erase(rfd); }

	conn.cgi->abort();
	delete conn.cgi;
	conn.cgi = NULL;
}

	// Preserve buffered pipelined bytes and body limit between requests.
void EventLoop::resetForKeepAlive(int fd) {
	ClientConnection &conn = connections[fd];
	std::string leftover = conn.request.rawBuffer;
	size_t      bodyLimit = conn.request.getMaxBodySize();
	conn.request = Request();
	conn.responseReady = false;
	conn.request.setMaxBodySize(bodyLimit);
	if (!leftover.empty()) {
		conn.request.parse(leftover);
		if (conn.request.getStatus() == Request::COMPLETE ||
		    conn.request.getStatus() == Request::ERROR) {
			// Process complete pipelined request immediately.
			handleCompleteRequest(fd);
			return;
		}
	}
	modifyToRead(fd);
}

void EventLoop::handleClientWrite(int fd) {
	ClientConnection &conn = connections[fd];

	if (conn.writeBuffer.empty()) {
		if (conn.keepAlive) {
			resetForKeepAlive(fd);
		} else if (conn.draining) {
			modifyToRead(fd);
		} else {
			removeClient(fd);
		}
		return;
	}

	ssize_t bytesSent = ::send(fd, conn.writeBuffer.data() + conn.writeOffset,
	                           conn.writeBuffer.size() - conn.writeOffset, 0);
	if (bytesSent < 0) {
		// Transient wakeup, same reasoning as in handleClientRead: real
		// errors are delivered as EPOLLERR and remove the client there.
		return;
	}
	if (bytesSent == 0) {
		// No progress possible on a writable socket: give up on the client.
		removeClient(fd);
		return;
	}

	// Advance an offset; erasing the front would memmove the rest each send.
	conn.writeOffset += static_cast<size_t>(bytesSent);
	conn.lastActivity = std::time(NULL);

	// If buffer is now fully sent, handle completion; otherwise EPOLLOUT fires again.
	if (conn.writeOffset >= conn.writeBuffer.size()) {
		std::string().swap(conn.writeBuffer); // release capacity
		conn.writeOffset = 0;
		if (conn.keepAlive) {
			resetForKeepAlive(fd);
		} else if (conn.draining) {
			modifyToRead(fd);
		} else {
			removeClient(fd);
		}
	}
}

void EventLoop::checkTimeouts() {
	std::time_t now = std::time(NULL);
	std::vector<int> toRemove;
	std::vector<int> cgiTimedOut;

	for (std::map<int, ClientConnection>::iterator it = connections.begin();
	     it != connections.end(); ++it) {
		// While a CGI is in progress its inactivity timeout governs the
		// connection; lastActivity on the client socket is stale by then.
		if (it->second.cgi != NULL) {
			if (it->second.cgi->checkTimeout())
				cgiTimedOut.push_back(it->first);
			continue;
		}
		if (now - it->second.lastActivity > CLIENT_TIMEOUT)
			toRemove.push_back(it->first);
	}

	for (size_t i = 0; i < cgiTimedOut.size(); ++i) {
		ClientConnection &conn = connections[cgiTimedOut[i]];
		int wfd = conn.cgi->getWriteFd();
		int rfd = conn.cgi->getReadFd();
		if (wfd >= 0) { removeFromPoll(wfd); cgiFdToClient.erase(wfd); }
		if (rfd >= 0) { removeFromPoll(rfd); cgiFdToClient.erase(rfd); }
		for (std::map<int, int>::iterator mi = cgiFdToClient.begin();
				mi != cgiFdToClient.end(); ) {
			if (mi->second == cgiTimedOut[i]) {
				removeFromPoll(mi->first);
				cgiFdToClient.erase(mi++);
			} else {
				++mi;
			}
		}
		completeCgi(conn);
	}

	for (size_t i = 0; i < toRemove.size(); ++i) {
		LOG("Timeout on fd=" + toString(toRemove[i]));
		removeClient(toRemove[i]);
	}
}
