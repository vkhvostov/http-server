#include "../../includes/http/CGI.hpp"
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <cctype>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctime>

CGI::CGI()
	: exitStatus(0), timedOut(false),
	  pid(-1), writeFd(-1), readFd(-1), envp(NULL),
	  bodyOffset(0), writeFinished(false), readFinished(false),
	  lastActivity(0)
{}

CGI::~CGI() {
	abort(); // safe even if start() never called
}

int CGI::getExitStatus() const
{
	return exitStatus;
}

bool CGI::hasTimedOut() const
{
	return timedOut;
}

// List of environment variables for the CGI script
std::vector<std::string> CGI::buildEnv(const Request& request,
										const std::string& scriptPath,
										int port,
										const std::string& serverName,
										const std::string& clientIP)
{
	std::vector<std::string> env;

	// Split request path into scriptName + queryString.
	// request.getPath() may contain "?foo=bar".
	std::string fullPath = request.getPath();
	std::string scriptName = fullPath;
	std::string queryString = "";
	size_t qPos = fullPath.find('?');
	if (qPos != std::string::npos)
	{
		scriptName = fullPath.substr(0, qPos);
		queryString = fullPath.substr(qPos + 1);
	}

	// Required CGI variables
	std::ostringstream portStream;
	portStream << port;

	env.push_back("REQUEST_METHOD="    + request.getMethod());
	env.push_back("SERVER_PROTOCOL="   + request.getVersion());
	env.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env.push_back("SERVER_SOFTWARE=http-server/1.0");
	env.push_back("SERVER_PORT="       + portStream.str());
	env.push_back("SERVER_NAME="       + serverName);
	env.push_back("REMOTE_ADDR=" + clientIP);
	env.push_back("REDIRECT_STATUS=200");

	// PATH_INFO carries the full request path; it is expected to match the
	// URL rebuilt from REQUEST_URI.
	env.push_back("SCRIPT_NAME="     + scriptName);
	env.push_back("SCRIPT_FILENAME=" + scriptPath);
	env.push_back("PATH_INFO="       + scriptName);
	env.push_back("PATH_TRANSLATED=" + scriptPath);
	env.push_back("QUERY_STRING="    + queryString);
	env.push_back("REQUEST_URI="     + fullPath);

	// CONTENT_LENGTH = actual body size
	std::ostringstream lenStream;
	lenStream << request.getBody().size();
	env.push_back("CONTENT_LENGTH=" + lenStream.str());

	// CONTENT_TYPE — only if header is present
	std::string contentType = request.getHeader("content-type");
	if (!contentType.empty())
		env.push_back("CONTENT_TYPE=" + contentType);

	// Every header becomes HTTP_<UPPER_NAME>
	const std::map<std::string, std::string>& headers = request.getHeaders();
	for (std::map<std::string, std::string>::const_iterator it = headers.begin();
			it != headers.end(); ++it)
	{
		const std::string& name = it->first;
		if (name == "content-type" || name == "content-length")
			continue;

		std::string envName = "HTTP_";
		for (size_t i = 0; i < name.size(); i++)
		{
			char c = name[i];
			if (c == '-')
				envName += '_';
			else
				envName += std::toupper(static_cast<unsigned char>(c));
		}
		env.push_back(envName + "=" + it->second);
	}

	return env;
}

// Convert std::vector<std::string> into a NULL-terminated char**
// (for execve())
char **CGI::vectorToEnvp(const std::vector<std::string>& env)
{
	char **envp = new char*[env.size() + 1];
	for (size_t i = 0; i < env.size(); i++)
	{
		envp[i] = new char[env[i].size() + 1];
		std::strcpy(envp[i], env[i].c_str());
	}
	envp[env.size()] = NULL;

	return envp;
}

// Walks the array, deletes each inner
// char[], then deletes the outer char* array. 
void CGI::freeEnvp(char **envp)
{
	if (!envp)
		return;
	for (int i = 0; envp[i] != NULL; i++)
		delete[] envp[i];

	delete[] envp;
}
int CGI::getWriteFd() const { return writeFd; }
int CGI::getReadFd() const { return readFd; }
bool CGI::isWriteFinished() const { return writeFinished; }
const std::string& CGI::getOutput() const { return output; }

// Rewrite a cwd-relative path so it still resolves after chdir into `dir`:
// one "../" per directory level. Returns the path unchanged if it is
// absolute or if `dir` climbs upward itself.
static std::string adjustForChdir(const std::string& path, const std::string& dir)
{
	if (path.empty() || path[0] == '/')
		return path;

	int depth = 0;
	size_t start = 0;
	while (start <= dir.size()) {
		size_t end = dir.find('/', start);
		if (end == std::string::npos) end = dir.size();
		std::string part = dir.substr(start, end - start);
		if (part == "..") depth--;
		else if (!part.empty() && part != ".") depth++;
		start = end + 1;
	}
	if (depth <= 0)
		return path;

	std::string rel = path;
	if (rel.size() >= 2 && rel[0] == '.' && rel[1] == '/')
		rel = rel.substr(2);
	std::string prefix;
	for (int i = 0; i < depth; ++i)
		prefix += "../";
	return prefix + rel;
}

// start: fork + pipes + exec
bool CGI::start(const Request& request,
				const std::string& scriptPath,
				const std::string& interpreterPath,
				int port,
				const std::string& serverName,
				const std::string& clientIP)
{
	exitStatus = 0;
	timedOut = false;
	writeFinished = false;
	readFinished = false;
	bodyOffset = 0;
	output.clear();
	body = request.getBody();
	lastActivity = time(NULL);

	signal(SIGPIPE, SIG_IGN);

	std::vector<std::string> envVec = buildEnv(request, scriptPath, port, serverName, clientIP);
	envp = vectorToEnvp(envVec);

	int pipeIn[2];
	int pipeOut[2];
	if (pipe(pipeIn) < 0 || pipe(pipeOut) < 0) {
		freeEnvp(envp);
		envp = NULL;
		return false;
	}

	// Close-on-exec so concurrent CGI children don't inherit these pipes.
	// The child's own stdin/stdout survive: dup2 clears the flag on its copy.
	fcntl(pipeIn[0], F_SETFD, FD_CLOEXEC);
	fcntl(pipeIn[1], F_SETFD, FD_CLOEXEC);
	fcntl(pipeOut[0], F_SETFD, FD_CLOEXEC);
	fcntl(pipeOut[1], F_SETFD, FD_CLOEXEC);

	pid = fork();
	if (pid < 0) {
		close(pipeIn[0]);  close(pipeIn[1]);
		close(pipeOut[0]); close(pipeOut[1]);
		freeEnvp(envp);
		envp = NULL;
		return false;
	}

	if (pid == 0) {
		if (dup2(pipeIn[0], STDIN_FILENO) < 0)  { freeEnvp(envp); std::exit(1); }
		if (dup2(pipeOut[1], STDOUT_FILENO) < 0){ freeEnvp(envp); std::exit(1); }
		close(pipeIn[0]);  close(pipeIn[1]);
		close(pipeOut[0]); close(pipeOut[1]);
		// Other server fds are close-on-exec, nothing else to clean up here.

		// Run the script from its own directory (enables relative file access).
		std::string interp = interpreterPath;
		std::string scriptArg = scriptPath;
		size_t slashPos = scriptPath.rfind('/');
		if (slashPos != std::string::npos) {
			std::string dir = scriptPath.substr(0, slashPos);
			if (!dir.empty()) {
				if (chdir(dir.c_str()) < 0) { freeEnvp(envp); std::exit(1); }
				interp = adjustForChdir(interp, dir);
			}
			scriptArg = scriptPath.substr(slashPos + 1);
		}

		char *argv[3];
		argv[0] = const_cast<char*>(interp.c_str());
		argv[1] = const_cast<char*>(scriptArg.c_str());
		argv[2] = NULL;
		execve(interp.c_str(), argv, envp);

		freeEnvp(envp);
		std::exit(1);
	}

	close(pipeIn[0]);
	close(pipeOut[1]);
	writeFd = pipeIn[1];
	readFd = pipeOut[0];

	fcntl(writeFd, F_SETFL, O_NONBLOCK);
	fcntl(readFd, F_SETFL, O_NONBLOCK);

	// If body is empty, close the write end now so the child sees EOF
	// immediately on stdin.
	if (body.empty()) {
		close(writeFd);
		writeFd = -1;
		writeFinished = true;
	}
	return true;
}

// Write the next chunk of body. True -> if more left,
// false if all body has been sent (or failed)
bool CGI::onWritable()
{
	if (writeFinished || writeFd < 0)
		return false;

	const size_t remaining = body.size() - bodyOffset;
	ssize_t n = write(writeFd, body.c_str() + bodyOffset, remaining);

	if (n < 0) {
		// Child closed its stdin; stop sending but keep reading its output.
		close(writeFd);
		writeFd = -1;
		writeFinished = true;
		std::string().swap(body);
		return false;
	}
	if (n == 0)
		return true;

	lastActivity = time(NULL);
	bodyOffset += static_cast<size_t>(n);
	if (bodyOffset >= body.size()) {
		close(writeFd);
		writeFd = -1;
		writeFinished = true;
		std::string().swap(body); // release the body copy
		return false;
	}
	return true;
}

// Read a chunk of output. True -> if more expected,
// false on EOF or error
bool CGI::onReadable()
{
	if (readFinished || readFd < 0)
		return false;

	// 64KB matches the kernel pipe buffer, so one read per event drains it.
	char buf[65536];
	ssize_t n = read(readFd, buf, sizeof(buf));

	if (n > 0) {
		output.append(buf, n);
		lastActivity = time(NULL);
		return true;
	}
	if (n == 0) {
		// EOF: script closed its stdout, output is complete.
		close(readFd);
		readFd = -1;
		readFinished = true;
		return false;
	}
	// Negative read on a transient wakeup: retry on the next event.
	// A dead script is caught by EOF or the inactivity timeout.
	return true;
}

bool CGI::checkTimeout()
{
	if (readFinished)
		return false;
	if (time(NULL) - lastActivity < CGI_TIMEOUT)
		return false;

	timedOut = true;
	if (pid > 0) kill(pid, SIGKILL);
	if (writeFd >= 0) { close(writeFd); writeFd = -1; writeFinished = true; }
	if (readFd >= 0) { close(readFd); readFd = -1; readFinished = true; }
	return true;
}

void CGI::finish()
{
	if (pid > 0) {
		int status;
		pid_t wp;
		do {
			wp = waitpid(pid, &status, 0);
		} while (wp < 0 && errno == EINTR);

		if (wp < 0)
			exitStatus = -1;
		else if (WIFEXITED(status))
			exitStatus = WEXITSTATUS(status);
		else
			exitStatus = -1;
		pid = -1;
	}
	if (writeFd >= 0) { close(writeFd); writeFd = -1; writeFinished = true; }
	if (readFd >= 0) { close(readFd); readFd = -1; readFinished = true; }
	if (envp) {
		freeEnvp(envp);
		envp = NULL;
	}
}

// Forced cleanup if the client disconnects mid-CGI
void CGI::abort()
{
	if (pid > 0)
		kill(pid, SIGKILL);
	finish();
}
