#include <cassert>
#include <iostream>

#include "http-server.hpp"

int main() {
	// Test 1: Create, bind, listen on ephemeral port
	{
		Socket s("0.0.0.0", 0);
		s.create();
		s.setReuseAddr();
		s.setNonBlocking();
		s.bind();
		s.listen();
		assert(s.getFd() >= 0);
		assert(s.getPort() > 0);
		assert(s.getHost() == "0.0.0.0");
		assert(s.isValid());
		std::cout << "Test 1 passed: socket created and listening on port " << s.getPort() << std::endl;
	}

	// Test 2: Can re-bind same port after close (SO_REUSEADDR + fd closed by destructor)
	{
		int boundPort;
		{
			Socket s1("0.0.0.0", 0);
			s1.create();
			s1.setReuseAddr();
			s1.bind();
			s1.listen();
			boundPort = s1.getPort();
		}
		Socket s2("0.0.0.0", boundPort);
		s2.create();
		s2.setReuseAddr();
		s2.bind();
		s2.listen();
		assert(s2.getPort() == boundPort);
		std::cout << "Test 2 passed: re-bind after close works" << std::endl;
	}

	// Test 3: Non-blocking accept returns -1 when no client is connected
	{
		Socket s("0.0.0.0", 0);
		s.create();
		s.setReuseAddr();
		s.setNonBlocking();
		s.bind();
		s.listen();
		int clientFd = s.accept();
		assert(clientFd == -1);
		std::cout << "Test 3 passed: non-blocking accept returns -1" << std::endl;
	}

	// Test 4: Two sockets on different ports simultaneously
	{
		Socket s1("0.0.0.0", 0);
		Socket s2("0.0.0.0", 0);
		s1.create(); s1.setReuseAddr(); s1.bind(); s1.listen();
		s2.create(); s2.setReuseAddr(); s2.bind(); s2.listen();
		assert(s1.getFd() != s2.getFd());
		assert(s1.getFd() >= 0);
		assert(s2.getFd() >= 0);
		assert(s1.getPort() != s2.getPort());
		std::cout << "Test 4 passed: multiple sockets on different ports" << std::endl;
	}

	// Test 5: Binding to loopback address
	{
		Socket s("127.0.0.1", 0);
		s.create();
		s.setReuseAddr();
		s.bind();
		s.listen();
		assert(s.getHost() == "127.0.0.1");
		assert(s.getPort() > 0);
		std::cout << "Test 5 passed: loopback bind" << std::endl;
	}

	// Test 6: Default constructor produces invalid socket
	{
		Socket s;
		assert(!s.isValid());
		assert(s.getFd() == -1);
		std::cout << "Test 6 passed: default constructor" << std::endl;
	}

	// Test 7: Binding same port twice throws on the second socket
	{
		Socket s1("0.0.0.0", 0);
		s1.create();
		s1.bind();
		s1.listen();
		int usedPort = s1.getPort();

		bool threw = false;
		try {
			Socket s2("0.0.0.0", usedPort);
			s2.create();
			s2.bind();
		} catch (const std::exception &e) {
			threw = true;
			std::string msg = e.what();
			assert(msg.find("bind()") != std::string::npos);
		}
		assert(threw);
		std::cout << "Test 7 passed: duplicate bind throws" << std::endl;
	}

	// Test 8: Explicit close makes socket invalid
	{
		Socket s("0.0.0.0", 0);
		s.create();
		s.setReuseAddr();
		s.bind();
		s.listen();
		assert(s.isValid());
		s.close();
		assert(!s.isValid());
		assert(s.getFd() == -1);
		std::cout << "Test 8 passed: close invalidates socket" << std::endl;
	}

	// Test 9: Double create() throws
	{
		Socket s("0.0.0.0", 0);
		s.create();
		bool threw = false;
		try {
			s.create();
		} catch (const std::exception &) {
			threw = true;
		}
		assert(threw);
		std::cout << "Test 9 passed: double create throws" << std::endl;
	}

	std::cout << "\nAll Phase 2 tests passed!" << std::endl;
	return 0;
}
