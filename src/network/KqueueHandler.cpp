#ifdef OS_MAC

#include "network/EventLoop.hpp"
#include "http-server.hpp"

#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

void EventLoop::initPoll() {
	pollFd = ::kqueue();
	if (pollFd < 0)
		throw std::runtime_error("kqueue() failed: " + std::string(strerror(errno)));

}

void EventLoop::addToRead(int fd) {
	struct kevent ev;
	EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
	if (::kevent(pollFd, &ev, 1, NULL, 0, NULL) < 0)
		throw std::runtime_error("kevent(EVFILT_READ, EV_ADD) failed: " + std::string(strerror(errno)));
}

void EventLoop::addToWrite(int fd) {
	struct kevent ev;
	EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, NULL);
	if (::kevent(pollFd, &ev, 1, NULL, 0, NULL) < 0)
		throw std::runtime_error("kevent(EVFILT_WRITE, EV_ADD) failed: " + std::string(strerror(errno)));
}

void EventLoop::modifyToRead(int fd) {
	// Ignore delete errors; filter may already be absent.
	struct kevent del;
	EV_SET(&del, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	::kevent(pollFd, &del, 1, NULL, 0, NULL);

	struct kevent add;
	EV_SET(&add, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
	if (::kevent(pollFd, &add, 1, NULL, 0, NULL) < 0)
		throw std::runtime_error("kevent(modifyToRead) failed: " + std::string(strerror(errno)));
}

void EventLoop::modifyToWrite(int fd) {
	// Temporarily switch to write-only while sending a response.
	struct kevent ev;
	EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	::kevent(pollFd, &ev, 1, NULL, 0, NULL);
	// Write filter registration must succeed.
	EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, NULL);
	if (::kevent(pollFd, &ev, 1, NULL, 0, NULL) < 0)
		LOG_ERR("kevent(EVFILT_WRITE, EV_ADD) failed: " + std::string(strerror(errno)));
}

void EventLoop::removeFromPoll(int fd) {
	struct kevent evs[2];
	EV_SET(&evs[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	EV_SET(&evs[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	if (::kevent(pollFd, evs, 2, NULL, 0, NULL) < 0 && errno != ENOENT)
		LOG_ERR("kevent(EV_DELETE) failed: " + std::string(strerror(errno)));
}

int EventLoop::waitForEvents() {
	struct timespec ts;
	ts.tv_sec = TIMEOUT_MS / 1000;
	ts.tv_nsec = (TIMEOUT_MS % 1000) * 1000000;
	return ::kevent(pollFd, NULL, 0, events, MAX_EVENTS, &ts);
}

int EventLoop::getEventFd(int index) {
	return static_cast<int>(events[index].ident);
}

bool EventLoop::isReadEvent(int index) {
	return events[index].filter == EVFILT_READ;
}

bool EventLoop::isWriteEvent(int index) {
	return events[index].filter == EVFILT_WRITE;
}

bool EventLoop::isErrorEvent(int index) {
	return (events[index].flags & EV_ERROR) != 0;
}

bool EventLoop::isEofEvent(int index) {
	return (events[index].flags & EV_EOF) != 0;
}

#endif
