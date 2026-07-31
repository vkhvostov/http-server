#ifndef CGI_HPP
#define CGI_HPP

#include "Request.hpp"
#include <string>
#include <vector>

class CGI
{
	public:
		CGI();
		~CGI();

		int  getExitStatus() const;
		bool hasTimedOut() const;
		bool start(const Request& request,
					const std::string& scriptPath,
					const std::string& interpreterPath,
					int port = 8080,
					const std::string& serverName = "localhost",
					const std::string& clientIP = "127.0.0.1");
		bool onWritable();
		bool onReadable();
		bool checkTimeout();
		void finish();
		void abort();

		int getWriteFd() const;
		int getReadFd() const;
		bool isWriteFinished() const;
		const std::string& getOutput() const;

	private:
		int  exitStatus;
		bool timedOut;

		pid_t pid;
		int writeFd; // pipeIn[1] (parent writes body here)
		int readFd; // pipeOut[0] (parent reads output here)
		char **envp; // freed in finish()
		std::string body; // copy of request body to send
		size_t bodyOffset; // how many body bytes written so far
		bool writeFinished; // true after close(writeFd)
		bool readFinished; // true after EOF on readFd
		std::string output; // accumulated output bytes
		time_t lastActivity; // last time the script made I/O progress

		// Inactivity limit: a script that makes no pipe progress for this long
		// is killed (504). Progressing transfers reset the timer, so large
		// concurrent uploads are not affected.
		static const int CGI_TIMEOUT = 10; // seconds

		std::vector<std::string> buildEnv(const Request& request,
											const std::string& scriptPath,
											int port,
											const std::string& serverName,
											const std::string& clientIP);
		char **vectorToEnvp(const std::vector<std::string>& env);
		void freeEnvp(char **envp);
};

#endif
