#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <map>
#include <string>
#include <vector>

struct RouteConfig {
	std::string path;
	std::vector<std::string> methods;
	std::string redirect;
	std::string root;
	bool directoryListing;
	std::string defaultFile;
	std::string uploadDir;
	std::string cgiExtension;
	std::string cgiPath;
	size_t maxBodySize; // 0 = inherit server-level limit

	RouteConfig();
};

struct ServerConfig {
	std::string host;
	int port;
	std::string serverName;
	std::map<int, std::string> errorPages;
	size_t clientMaxBodySize;
	std::vector<RouteConfig> routes;

	ServerConfig();
};

class Config {
	public:
		Config();
		~Config();

		void parse(const std::string &filePath);
		const std::vector<ServerConfig> &getServers() const;
		std::string getRoot() const;

	private:
		std::vector<ServerConfig> servers;

		std::vector<std::string> tokenize(const std::string &content);
		void parseServer(std::vector<std::string> &tokens, size_t &pos);
		void parseLocation(std::vector<std::string> &tokens, size_t &pos, ServerConfig &server);
		void parseDirective(const std::string &key, std::vector<std::string> &tokens, size_t &pos, ServerConfig &server);
		void parseRouteDirective(const std::string &key, std::vector<std::string> &tokens, size_t &pos, RouteConfig &route);
		void validate();
};

#endif
