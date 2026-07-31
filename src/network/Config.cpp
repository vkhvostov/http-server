#include "network/Config.hpp"
#include "http-server.hpp"

#include <cerrno>
#include <climits>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace {

static const size_t kReadBufferSize = 4096;

std::string toUpperAscii(const std::string &value) {
	std::string upper = value;
	for (size_t i = 0; i < upper.size(); ++i) {
		upper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(upper[i])));
	}
	return upper;
}

bool isSupportedHttpMethod(const std::string &method) {
	return method == "GET" || method == "POST" || method == "DELETE";
}

std::string normalizeAndValidateHttpMethod(const std::string &rawMethod) {
	std::string normalized = toUpperAscii(rawMethod);
	if (!isSupportedHttpMethod(normalized)) {
		throw std::runtime_error("Config parse error: unsupported HTTP method '" + rawMethod + "' (allowed: GET, POST, DELETE)");
	}
	return normalized;
}

class FdGuard {
public:
	explicit FdGuard(int fdValue) : fd(fdValue) {
	}

	~FdGuard() {
		if (fd >= 0) {
			close(fd);
		}
	}

	int get() const {
		return fd;
	}

	int release() {
		int raw = fd;
		fd = -1;
		return raw;
	}

private:
	FdGuard(const FdGuard &);
	FdGuard &operator=(const FdGuard &);

	int fd;
};

void expectToken(const std::vector<std::string> &tokens, size_t pos, const std::string &expected) {
	if (pos >= tokens.size()) {
		throw std::runtime_error("Config parse error: expected '" + expected + "' but reached end of file");
	}
	if (tokens[pos] != expected) {
		throw std::runtime_error("Config parse error: expected '" + expected + "' at token #" + toString(pos) + ", got '" + tokens[pos] + "'");
	}
}

bool isDigitsOnly(const std::string &value) {
	if (value.empty()) {
		return false;
	}
	for (size_t i = 0; i < value.size(); ++i) {
		if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
			return false;
		}
	}
	return true;
}

long parsePositiveLong(const std::string &raw, const std::string &fieldName) {
	if (!isDigitsOnly(raw)) {
		throw std::runtime_error("Config parse error: invalid " + fieldName + " value '" + raw + "'");
	}
	errno = 0;
	char *end = NULL;
	long value = std::strtol(raw.c_str(), &end, 10);
	if (errno != 0 || end == NULL || *end != '\0' || value <= 0) {
		throw std::runtime_error("Config parse error: invalid " + fieldName + " value '" + raw + "'");
	}
	return value;
}

size_t parseSizeBytes(const std::string &value) {
	if (value.empty()) {
		throw std::runtime_error("Config parse error: empty client_max_body_size");
	}

	char suffix = value[value.size() - 1];
	std::string numberPart = value;
	size_t multiplier = 1;

	if (std::isalpha(static_cast<unsigned char>(suffix))) {
		numberPart = value.substr(0, value.size() - 1);
		if (numberPart.empty()) {
			throw std::runtime_error("Config parse error: invalid client_max_body_size '" + value + "'");
		}
		if (suffix == 'K' || suffix == 'k') {
			multiplier = 1024;
		} else if (suffix == 'M' || suffix == 'm') {
			multiplier = 1024 * 1024;
		} else if (suffix == 'G' || suffix == 'g') {
			multiplier = 1024 * 1024 * 1024;
		} else {
			throw std::runtime_error("Config parse error: unsupported size suffix in '" + value + "'");
		}
	}

	long base = parsePositiveLong(numberPart, "client_max_body_size");
	if (multiplier > 1 &&
		base > static_cast<long>(std::numeric_limits<size_t>::max() / multiplier)) {
		throw std::runtime_error("Config parse error: client_max_body_size is too large: '" + value + "'");
	}
	return static_cast<size_t>(base) * multiplier;
}

std::string readWholeFile(const std::string &filePath) {
	int fd = open(filePath.c_str(), O_RDONLY);
	if (fd < 0) {
		throw std::runtime_error("Config parse error: failed to open '" + filePath + "': " + std::strerror(errno));
	}
	FdGuard fdGuard(fd);

	std::string content;
	char buffer[kReadBufferSize];
	while (true) {
		ssize_t bytesRead = read(fdGuard.get(), buffer, sizeof(buffer));
		if (bytesRead < 0) {
			throw std::runtime_error("Config parse error: failed to read '" + filePath + "': " + std::strerror(errno));
		}
		if (bytesRead == 0) {
			break;
		}
		content.append(buffer, static_cast<size_t>(bytesRead));
	}

	if (close(fdGuard.release()) < 0) {
		throw std::runtime_error("Config parse error: failed to close '" + filePath + "': " + std::strerror(errno));
	}

	return content;
}

}

RouteConfig::RouteConfig()
	: path(), methods(), redirect(), root(), directoryListing(false), defaultFile("index.html"), uploadDir(), cgiExtension(), cgiPath(), maxBodySize(0) {
}

ServerConfig::ServerConfig()
	: host("0.0.0.0"), port(8080), serverName(), errorPages(), clientMaxBodySize(1024 * 1024), routes() {
}

Config::Config() : servers() {
}

Config::~Config() {
}

const std::vector<ServerConfig> &Config::getServers() const {
	return servers;
}

std::string Config::getRoot() const {
	if (!servers.empty() && !servers[0].routes.empty() && !servers[0].routes[0].root.empty()) {
		return servers[0].routes[0].root;
	}
	return "./www";
}

std::vector<std::string> Config::tokenize(const std::string &content) {
	std::vector<std::string> tokens;
	std::string current;

	for (size_t i = 0; i < content.size(); ++i) {
		char c = content[i];

		if (c == '#') {
			while (i < content.size() && content[i] != '\n') {
				++i;
			}
			if (!current.empty()) {
				tokens.push_back(current);
				current.clear();
			}
			continue;
		}

		if (c == '{' || c == '}' || c == ';') {
			if (!current.empty()) {
				tokens.push_back(current);
				current.clear();
			}
			tokens.push_back(std::string(1, c));
			continue;
		}

		if (std::isspace(static_cast<unsigned char>(c))) {
			if (!current.empty()) {
				tokens.push_back(current);
				current.clear();
			}
			continue;
		}

		current.push_back(c);
	}

	if (!current.empty()) {
		tokens.push_back(current);
	}

	return tokens;
}

void Config::parse(const std::string &filePath) {
	servers.clear();

	std::string content = readWholeFile(filePath);
	std::vector<std::string> tokens = tokenize(content);
	size_t pos = 0;

	while (pos < tokens.size()) {
		if (tokens[pos] != "server") {
			throw std::runtime_error("Config parse error: unexpected token '" + tokens[pos] + "' at token #" + toString(pos));
		}
		++pos;
		expectToken(tokens, pos, "{");
		++pos;
		parseServer(tokens, pos);
	}

	if (servers.empty()) {
		throw std::runtime_error("Config parse error: no server blocks found");
	}

	validate();
}

void Config::parseServer(std::vector<std::string> &tokens, size_t &pos) {
	ServerConfig server;

	while (pos < tokens.size() && tokens[pos] != "}") {
		if (tokens[pos] == "location") {
			++pos;
			parseLocation(tokens, pos, server);
		} else {
			parseDirective(tokens[pos], tokens, pos, server);
		}
	}

	expectToken(tokens, pos, "}");
	++pos;
	servers.push_back(server);
}

void Config::parseLocation(std::vector<std::string> &tokens, size_t &pos, ServerConfig &server) {
	if (pos >= tokens.size()) {
		throw std::runtime_error("Config parse error: expected location path");
	}

	RouteConfig route;
	route.path = tokens[pos];
	++pos;

	expectToken(tokens, pos, "{");
	++pos;

	while (pos < tokens.size() && tokens[pos] != "}") {
		parseRouteDirective(tokens[pos], tokens, pos, route);
	}

	expectToken(tokens, pos, "}");
	++pos;

	server.routes.push_back(route);
}

void Config::parseDirective(const std::string &key, std::vector<std::string> &tokens, size_t &pos, ServerConfig &server) {
	++pos;

	if (key == "listen") {
		if (pos >= tokens.size()) {
			throw std::runtime_error("Config parse error: missing listen value");
		}

		const std::string value = tokens[pos++];
		size_t sepPos = value.find(':');
		std::string portPart = (sepPos != std::string::npos) ? value.substr(sepPos + 1) : value;
		if (sepPos != std::string::npos) {
			server.host = value.substr(0, sepPos);
		}
		long portLong = parsePositiveLong(portPart, "listen port");
		if (portLong < 1 || portLong > 65535) {
			throw std::runtime_error("Config parse error: listen port out of range (1-65535): '" + portPart + "'");
		}
		server.port = static_cast<int>(portLong);

		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	if (key == "server_name") {
		if (pos >= tokens.size()) {
			throw std::runtime_error("Config parse error: missing server_name value");
		}
		server.serverName = tokens[pos++];
		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	if (key == "error_page") {
		if (pos + 1 >= tokens.size()) {
			throw std::runtime_error("Config parse error: error_page requires code and file path");
		}
		std::string codeToken = tokens[pos++];
		long codeLong = parsePositiveLong(codeToken, "error_page status code");
		if (codeLong < 100 || codeLong > 599) {
			throw std::runtime_error("Config parse error: error_page status code out of range (100-599): '" + codeToken + "'");
		}
		int code = static_cast<int>(codeLong);
		std::string path = tokens[pos++];
		server.errorPages[code] = path;
		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	if (key == "client_max_body_size") {
		if (pos >= tokens.size()) {
			throw std::runtime_error("Config parse error: missing client_max_body_size value");
		}
		server.clientMaxBodySize = parseSizeBytes(tokens[pos++]);
		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	throw std::runtime_error("Config parse error: unknown server directive '" + key + "'");
}

void Config::parseRouteDirective(const std::string &key, std::vector<std::string> &tokens, size_t &pos, RouteConfig &route) {
	++pos;

	if (key == "methods" || key == "allow_methods") {
		if (pos >= tokens.size()) {
			throw std::runtime_error("Config parse error: missing methods list");
		}
		std::set<std::string> uniqueMethods;
		while (pos < tokens.size() && tokens[pos] != ";") {
			std::string normalizedMethod = normalizeAndValidateHttpMethod(tokens[pos]);
			if (uniqueMethods.insert(normalizedMethod).second) {
				route.methods.push_back(normalizedMethod);
			}
			++pos;
		}
		if (route.methods.empty()) {
			throw std::runtime_error("Config parse error: methods list cannot be empty");
		}
		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	if (key == "return") {
		if (pos >= tokens.size()) {
			throw std::runtime_error("Config parse error: missing return value");
		}
		route.redirect = tokens[pos++];
		if (pos < tokens.size() && tokens[pos] != ";") {
			route.redirect += " " + tokens[pos++];
		}
		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	if (key == "root") {
		if (pos >= tokens.size()) {
			throw std::runtime_error("Config parse error: missing root value");
		}
		route.root = tokens[pos++];
		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	if (key == "autoindex") {
		if (pos >= tokens.size()) {
			throw std::runtime_error("Config parse error: missing autoindex value");
		}
		if (tokens[pos] == "on") {
			route.directoryListing = true;
		} else if (tokens[pos] == "off") {
			route.directoryListing = false;
		} else {
			throw std::runtime_error("Config parse error: autoindex must be 'on' or 'off'");
		}
		++pos;
		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	if (key == "index") {
		if (pos >= tokens.size()) {
			throw std::runtime_error("Config parse error: missing index value");
		}
		route.defaultFile = tokens[pos++];
		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	if (key == "upload_dir") {
		if (pos >= tokens.size()) {
			throw std::runtime_error("Config parse error: missing upload_dir value");
		}
		route.uploadDir = tokens[pos++];
		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	if (key == "cgi_extension") {
		if (pos >= tokens.size()) {
			throw std::runtime_error("Config parse error: missing cgi_extension value");
		}
		route.cgiExtension = tokens[pos++];
		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	if (key == "cgi_path") {
		if (pos >= tokens.size()) {
			throw std::runtime_error("Config parse error: missing cgi_path value");
		}
		route.cgiPath = tokens[pos++];
		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	if (key == "client_max_body_size") {
		if (pos >= tokens.size()) {
			throw std::runtime_error("Config parse error: missing client_max_body_size value");
		}
		route.maxBodySize = parseSizeBytes(tokens[pos++]);
		expectToken(tokens, pos, ";");
		++pos;
		return;
	}

	throw std::runtime_error("Config parse error: unknown location directive '" + key + "'");
}

void Config::validate() {
	std::set<std::string> usedBindings;

	for (size_t i = 0; i < servers.size(); ++i) {
		ServerConfig &server = servers[i];

		if (server.clientMaxBodySize == 0) {
			throw std::runtime_error("Config validation error: client_max_body_size must be > 0");
		}

		std::ostringstream binding;
		binding << server.host << ":" << server.port;
		if (usedBindings.find(binding.str()) != usedBindings.end()) {
			throw std::runtime_error("Config validation error: duplicate listen binding '" + binding.str() + "'");
		}
		usedBindings.insert(binding.str());

		std::set<std::string> seenPaths;
		for (size_t r = 0; r < server.routes.size(); ++r) {
			RouteConfig &route = server.routes[r];

			if (route.path.empty() || route.path[0] != '/') {
				throw std::runtime_error("Config validation error: location path must start with '/': '" + route.path + "'");
			}

			if (seenPaths.find(route.path) != seenPaths.end()) {
				throw std::runtime_error("Config validation error: duplicate location path '" + route.path + "' in server '" + server.host + ":" + toString(static_cast<size_t>(server.port)) + "'");
			}
			seenPaths.insert(route.path);

			if (!route.root.empty() && access(route.root.c_str(), R_OK) != 0) {
				std::cerr << "[http-server warning] unreadable route root: " << route.root << std::endl;
			}

			bool hasExt = !route.cgiExtension.empty();
			bool hasPath = !route.cgiPath.empty();
			if (hasExt != hasPath) {
				throw std::runtime_error("Config validation error: cgi_extension and cgi_path must be set together for location '" + route.path + "'");
			}

			if (hasPath && access(route.cgiPath.c_str(), X_OK) != 0) {
				std::cerr << "[http-server warning] cgi_path is not executable or missing on this machine: "
				          << route.cgiPath << std::endl;
			}
		}
	}
}
