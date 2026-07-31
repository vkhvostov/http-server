#ifdef OS_LINUX

#include "network/EventLoop.hpp"
#include "http-server.hpp"

#include <stdexcept>

void EventLoop::initPoll() {
	// Size hint is ignored by modern kernels but must be > 0.
	pollFd = ::epoll_create(MAX_EVENTS);
	if (pollFd < 0)
		throw std::runtime_error("epoll_create() failed: " + std::string(strerror(errno)));
	if (::fcntl(pollFd, F_SETFD, FD_CLOEXEC) < 0)
		throw std::runtime_error("fcntl(F_SETFD, FD_CLOEXEC) failed: " + std::string(strerror(errno)));
}

void EventLoop::addToRead(int fd) {
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = fd;
	if (::epoll_ctl(pollFd, EPOLL_CTL_ADD, fd, &ev) < 0)
		throw std::runtime_error("epoll_ctl(EPOLL_CTL_ADD, EPOLLIN) failed: " + std::string(strerror(errno)));
}

void EventLoop::addToWrite(int fd) {
	struct epoll_event ev;
	ev.events = EPOLLOUT;
	ev.data.fd = fd;
	if (::epoll_ctl(pollFd, EPOLL_CTL_ADD, fd, &ev) < 0)
		throw std::runtime_error("epoll_ctl(EPOLL_CTL_ADD, EPOLLOUT) failed: " + std::string(strerror(errno)));
}

void EventLoop::modifyToRead(int fd) {
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = fd;
	if (::epoll_ctl(pollFd, EPOLL_CTL_MOD, fd, &ev) < 0)
		throw std::runtime_error("epoll_ctl(EPOLL_CTL_MOD, EPOLLIN) failed: " + std::string(strerror(errno)));
}

void EventLoop::modifyToWrite(int fd) {
	struct epoll_event ev;
	ev.events = EPOLLOUT;
	ev.data.fd = fd;
	if (::epoll_ctl(pollFd, EPOLL_CTL_MOD, fd, &ev) < 0)
		throw std::runtime_error("epoll_ctl(EPOLL_CTL_MOD, EPOLLOUT) failed: " + std::string(strerror(errno)));
}

void EventLoop::removeFromPoll(int fd) {
	if (::epoll_ctl(pollFd, EPOLL_CTL_DEL, fd, NULL) < 0 && errno != ENOENT && errno != EBADF)
		LOG_ERR("epoll_ctl(EPOLL_CTL_DEL) failed: " + std::string(strerror(errno)));
}

int EventLoop::waitForEvents() {
	return ::epoll_wait(pollFd, events, MAX_EVENTS, TIMEOUT_MS);
}

int EventLoop::getEventFd(int index) {
	return events[index].data.fd;
}

bool EventLoop::isReadEvent(int index) {
	return (events[index].events & EPOLLIN) != 0;
}

bool EventLoop::isWriteEvent(int index) {
	return (events[index].events & EPOLLOUT) != 0;
}

bool EventLoop::isErrorEvent(int index) {
	return (events[index].events & EPOLLERR) != 0;
}

bool EventLoop::isEofEvent(int index) {
	return (events[index].events & EPOLLHUP) != 0;
}

#endif
