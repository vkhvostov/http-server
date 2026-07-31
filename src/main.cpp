#include "network/Config.hpp"
#include "network/EventLoop.hpp"
#include "network/Socket.hpp"
#include "http-server.hpp"

#include <cstdlib>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
	EventLoop *gLoop = NULL;

	void signalHandler(int) {
		if (gLoop)
			gLoop->stop();
	}

	int runServer(int argc, char **argv) {
		Config config;
		std::vector<ServerConfig> servers;

		if (argc > 2) {
			std::cerr << "Usage: " << argv[0] << " [configuration file]" << std::endl;
			return EXIT_FAILURE;
		}

		const std::string configPath = (argc == 2) ? argv[1] : "configs/default.conf";
		try {
			config.parse(configPath);
		} catch (const std::exception &e) {
			LOG_ERR(std::string("Config parse error: ") + e.what());
			return EXIT_FAILURE;
		}
		servers = config.getServers();

		std::vector<Socket *> sockets;
		EventLoop loop;
		loop.setConfig(&config);
		gLoop = &loop;

		signal(SIGINT, signalHandler);
		signal(SIGTERM, signalHandler);
		signal(SIGPIPE, SIG_IGN);

		try {
			loop.init();

			std::set<std::string> bound;
			for (size_t i = 0; i < servers.size(); ++i) {
				std::string addr = servers[i].host + ":" + toString(servers[i].port);
				if (bound.count(addr))
					throw std::runtime_error("duplicate listen address in config: " + addr);
				Socket *sock = new Socket(servers[i].host, servers[i].port);
				sockets.push_back(sock);
				sock->create();
				sock->setReuseAddr();
				sock->setNonBlocking();
				sock->bind();
				sock->listen();
				loop.addListenSocket(sock->getFd(), sock->getPort());
				bound.insert(addr);
				LOG("Listening on " + addr);
			}

			loop.run();
		} catch (const std::exception &e) {
			LOG_ERR(std::string("Fatal: ") + e.what());
			for (size_t i = 0; i < sockets.size(); ++i)
				delete sockets[i];
			return EXIT_FAILURE;
		}

		for (size_t i = 0; i < sockets.size(); ++i)
			delete sockets[i];
		return EXIT_SUCCESS;
	}
}

int main(int argc, char **argv) {
	return runServer(argc, argv);
}
