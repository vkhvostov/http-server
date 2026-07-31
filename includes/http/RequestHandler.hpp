#ifndef REQUESTHANDLER_HPP
#define REQUESTHANDLER_HPP

#include "Request.hpp"
#include "Response.hpp"
#include "Config.hpp"
#include <string>

class RequestHandler {
	public:
		RequestHandler();
		~RequestHandler();

		Response handleRequest(const Request& req,
								const ServerConfig& srv,
								const RouteConfig* route);
		Response makeErrorResponse(int code, const std::string& msg,
									const ServerConfig* srv = NULL);
		Response parseCGIOutput(const std::string& output);

	private:
		static const size_t MAX_BUF_SIZE;
		static const size_t MAX_STATIC_FILE_SIZE;

		Response serveStatic(const std::string& path);
		Response handleDelete(const std::string& path);
		Response generateAutoindex(const std::string& fsPath, const std::string& uriPath);

		void splitPathAndQuery(const std::string& full,
									std::string& pathOut,
									std::string& queryOut);

		std::string escapeHTML(const std::string& data);
		bool hasPathTraversal(const std::string& path);
		std::string getAllowedMethods(const RouteConfig* route) const;
		Response loadErrorPage(int code, const std::string& msg,
								const ServerConfig& srv);

		// Upload helpers
		Response handleUpload(const Request& req, const RouteConfig& route,
								const ServerConfig* srv);
		std::string sanitizeFilename(const std::string& name);
		bool saveFile(const std::string& dir, const std::string& name,
						const std::string& data);
};

#endif
