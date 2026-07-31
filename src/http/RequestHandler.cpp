#include "RequestHandler.hpp"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>

const size_t RequestHandler::MAX_BUF_SIZE = 4096;
const size_t RequestHandler::MAX_STATIC_FILE_SIZE = 10485760; // 10MB

RequestHandler::RequestHandler() {};
RequestHandler::~RequestHandler() {};

Response RequestHandler::handleRequest(const Request& req,
										const ServerConfig& srv,
										const RouteConfig* route)
{
	if (req.getStatus() == Request::ERROR)
		return makeErrorResponse(req.getErrorCode(), req.getErrorMessage(), &srv);

	std::string cleanPath, queryStr;
	splitPathAndQuery(req.getPath(), cleanPath, queryStr);

	if (hasPathTraversal(cleanPath))
		return makeErrorResponse(403, "Forbidden", &srv);

	// Check allowed methods — strict check, no implicit HEAD.
	if (route != NULL && !route->methods.empty()) {
		bool allowed = false;
		const std::string method = req.getMethod();
		for (size_t i = 0; i < route->methods.size(); ++i) {
			if (route->methods[i] == method) { allowed = true; break; }
		}
		if (!allowed) {
			Response res = makeErrorResponse(405, "Method Not Allowed", &srv);
			res.headers["Allow"] = getAllowedMethods(route);
			return res;
		}
	}

	// If the matched route path is cleanPath + "/" the URI is missing its
	// trailing slash — redirect before doing anything else.
	if (route != NULL && !route->path.empty() && route->path == cleanPath + "/") {
		std::string loc = req.getPath();
		if (loc.empty() || loc[loc.size() - 1] != '/')
			loc += "/";
		Response res;
		res.statusCode = 301;
		res.statusMessage = "Moved Permanently";
		res.headers["Location"] = loc;
		return res;
	}

	// Evaluate redirect before touching the filesystem
	if (route != NULL && !route->redirect.empty()) {
		// Redirect value is either "code url" or just "url"
		std::string redirectVal = route->redirect;
		int code = 301;
		std::string location;
		size_t space = redirectVal.find(' ');
		if (space != std::string::npos) {
			std::string codeStr = redirectVal.substr(0, space);
			code = std::atoi(codeStr.c_str());
			if (code == 0) code = 301;
			location = redirectVal.substr(space + 1);
		} else {
			location = redirectVal;
		}
		Response res;
		res.statusCode = code;
		res.statusMessage = Response::getReasonPhrase(code);
		res.headers["Location"] = location;
		return res;
	}

	// Resolve per-route or global root
	std::string root = "./www";
	if (route != NULL && !route->root.empty())
		root = route->root;
	else if (!srv.routes.empty() && !srv.routes[0].root.empty())
		root = srv.routes[0].root;

	// Strip the route prefix from cleanPath so that root + suffix is correct (no duplications)
	std::string suffix = cleanPath;
	if (route != NULL && !route->path.empty() && route->path != "/") {
		if (suffix.size() >= route->path.size() &&
			suffix.substr(0, route->path.size()) == route->path)
			suffix = suffix.substr(route->path.size());
		if (suffix.empty()) suffix = "/";
		if (!suffix.empty() && suffix[0] != '/')
			suffix = "/" + suffix;
	}

	// Ensure "/" separator between root and suffix if needed.
	std::string path = root;
	if (!root.empty() && root[root.size() - 1] != '/' && !suffix.empty() && suffix[0] != '/')
		path += "/";
	path += suffix;

	// POST to a route with an uploadDir -> handle upload before any stat,
	// since the uploaded file doesn't exist yet
	if (req.getMethod() == "POST" && route != NULL && !route->uploadDir.empty())
		return handleUpload(req, *route, &srv);

	// POST allowed on this route but no uploadDir and no CGI matched:
	// accept the body and answer with an empty 200 (e.g. /post_body).
	if (req.getMethod() == "POST") {
		Response res;
		res.statusCode = 200;
		res.statusMessage = "OK";
		res.headers["Content-Type"] = "text/html";
		return res;
	}

	struct stat info;
	if (stat(path.c_str(), &info) != 0)
		return makeErrorResponse(404, "Not Found", &srv);

	if (S_ISDIR(info.st_mode)) {
		std::string originalUri = req.getPath();
		const std::string& method = req.getMethod();
		bool isReadMethod = (method == "GET" || method == "HEAD");

		// Non-read methods (DELETE, POST without uploadDir) on a directory are invalid
		if (!isReadMethod)
			return makeErrorResponse(403, "Forbidden", &srv);

		// Redirect GET/HEAD to the slash-terminated URI
		if (!originalUri.empty() && originalUri[originalUri.length() - 1] != '/') {
			Response res;
			res.statusCode = 301;
			res.statusMessage = "Moved Permanently";
			res.headers["Location"] = originalUri + "/";
			return res;
		}

		// Try index file
		std::string defaultFile = "index.html";
		if (route != NULL && !route->defaultFile.empty())
			defaultFile = route->defaultFile;

		std::string indexPath = path;
		if (indexPath[indexPath.length() - 1] != '/')
			indexPath += "/";
		indexPath += defaultFile;

		struct stat indexInfo;
		if (stat(indexPath.c_str(), &indexInfo) == 0)
			return serveStatic(indexPath);

		bool autoindex = false;
		if (route != NULL)
			autoindex = route->directoryListing;

		if (autoindex)
			return generateAutoindex(path, req.getPath());

		return makeErrorResponse(404, "Not Found", &srv);
	}

	if (S_ISREG(info.st_mode)) {
		if (req.getMethod() == "DELETE")
			return handleDelete(path);
		return serveStatic(path);
	}

	return makeErrorResponse(500, "Internal Server Error", &srv);
}

std::string RequestHandler::getAllowedMethods(const RouteConfig* route) const {
	if (route == NULL || route->methods.empty())
		return "GET, POST, DELETE";
	std::string result;
	for (size_t i = 0; i < route->methods.size(); ++i) {
		if (i > 0) result += ", ";
		result += route->methods[i];
	}
	return result;
}

void RequestHandler::splitPathAndQuery(const std::string& full,
										std::string& pathOut,
										std::string& queryOut)
{
	size_t qPos = full.find('?');
	if (qPos != std::string::npos) {
		pathOut = full.substr(0, qPos);
		queryOut = full.substr(qPos + 1);
	} else {
		pathOut = full;
		queryOut = "";
	}
}

Response RequestHandler::parseCGIOutput(const std::string& output)
{
	Response res;
	res.statusCode = 200;
	res.statusMessage = "OK";

	// Find header/body separator. "\r\n\r\n" first, then "\n\n".
	size_t sepPos = output.find("\r\n\r\n");
	size_t sepLen = 4;
	if (sepPos == std::string::npos) {
		sepPos = output.find("\n\n");
		sepLen = 2;
	}

	// No header block at all: malformed CGI response (RFC 3875 -> 500).
	if (sepPos == std::string::npos)
		return makeErrorResponse(500, "Internal Server Error");

	std::string headersStr = output.substr(0, sepPos);
	res.body = output.substr(sepPos + sepLen);

	// Parse each header line
	std::istringstream stream(headersStr);
	std::string line;
	while (std::getline(stream, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.empty()) continue;

		size_t colonPos = line.find(':');
		if (colonPos == std::string::npos) continue;

		std::string key = line.substr(0, colonPos);
		std::string value = line.substr(colonPos + 1);

		size_t firstNonSpace = value.find_first_not_of(" \t");
		value = (firstNonSpace == std::string::npos)
				? std::string("")
				: value.substr(firstNonSpace);

		if (key == "Status") {
			// "200 OK" -> split into code + message
			std::istringstream iss(value);
			int code;
			iss >> code;
			res.statusCode = code;
			std::string msg;
			std::getline(iss, msg);
			if (!msg.empty() && msg[0] == ' ')
				msg = msg.substr(1);
			if (!msg.empty())
				res.statusMessage = msg;
		} else {
			res.headers[key] = value;
		}
	}

	// CGI redirect: if the script returned a "Location:" header but 
	// not set a Status -> 302 Found redirect
	if (res.statusCode == 200 &&
		res.headers.find("Location") != res.headers.end())
	{
		res.statusCode = 302;
		res.statusMessage = "Found";
	}

	return res;
}

// Remove a regular file from disk. 204 No Content on success.
// Maps errors to codes: EACCES -> 403, ENOENT -> 404, else -> 500
Response RequestHandler::handleDelete(const std::string& path) {
	if (std::remove(path.c_str()) == 0) {
		Response res;
		res.statusCode = 204;
		res.statusMessage = "No Content";
		// No body; Content-Length to 0 automatically
		return res;
	}
	if (errno == EACCES || errno == EPERM)
		return makeErrorResponse(403, "Forbidden");
	if (errno == ENOENT)
		return makeErrorResponse(404, "Not Found");
	return makeErrorResponse(500, "Internal Server Error");
}

// For reading the file
Response RequestHandler::serveStatic(const std::string& path) {
	struct stat info;
	if (stat(path.c_str(), &info) != 0)
		return makeErrorResponse(404, "Not Found");

	if (info.st_size < 0 || (size_t)info.st_size > MAX_STATIC_FILE_SIZE)
		return makeErrorResponse(413, "Payload Too Large");

	int fd = open(path.c_str(), O_RDONLY);
	if (fd == -1) {
		if (errno == EACCES)
			return makeErrorResponse(403, "Forbidden");
		if (errno == ENOENT)
			return makeErrorResponse(404, "Not Found");
		return makeErrorResponse(500, "Internal Server Error");
	}

	std::string body;
	body.reserve(info.st_size);
	char buf[MAX_BUF_SIZE];
	ssize_t bytesRead;

	while ((bytesRead = read(fd, buf, sizeof(buf))) > 0)
		body.append(buf, bytesRead);
	close(fd);

	if (bytesRead == -1)
		return makeErrorResponse(500, "Internal Server Error");
	
	Response res;
	res.statusCode = 200;
	res.statusMessage = "OK";
	res.body = body;
	res.headers["Content-Type"] = Response::getMimeType(path);
	return res;
}

std::string RequestHandler::escapeHTML(const std::string& data) {
	std::string buffer;
	buffer.reserve(data.size());
	for (size_t i = 0; i < data.size(); ++i) {
		switch (data[i]) {
			case '&':  buffer.append("&amp;"); break;
			case '\"': buffer.append("&quot;"); break;
			case '\'': buffer.append("&apos;"); break;
			case '<':  buffer.append("&lt;"); break;
			case '>':  buffer.append("&gt;"); break;
			default:   buffer.append(1, data[i]); break;
		}
	}
	return buffer;
}

// Generates a page with all the files/folders in the specif.directory
Response RequestHandler::generateAutoindex(const std::string& fsPath, const std::string& uriPath) {
	DIR* dir = opendir(fsPath.c_str());
	if (!dir)
		return makeErrorResponse(403, "Forbidden");

	// Collect entries first so we can sort them
	std::vector<std::string> entries;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		std::string name = entry->d_name;
		if (name == "." || name == "..") continue;
		entries.push_back(name);
	}
	closedir(dir);
	std::sort(entries.begin(), entries.end());

	std::stringstream ss;
	ss << "<!doctype html><html lang=\"en\"><head>"
	   << "<meta charset=\"UTF-8\">"
	   << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
	   << "<title>Index of " << uriPath << "</title>"
	   << "<style>"
	   << "*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }"
	   << "body { font-family: system-ui, -apple-system, sans-serif; background: #eef2f7;"
	   << "  color: #1a1a2e; min-height: 100vh; display: flex; align-items: center;"
	   << "  justify-content: center; padding: 32px 16px; }"
	   << ".card { background: #fff; border-radius: 14px; padding: 44px 52px;"
	   << "  max-width: 520px; width: 100%; box-shadow: 0 4px 24px rgba(0,0,0,0.08); }"
	   << ".label { font-size: 11px; font-weight: 700; letter-spacing: 0.1em;"
	   << "  text-transform: uppercase; color: #6c7be8; margin-bottom: 14px; }"
	   << "h1 { font-size: 22px; font-weight: 700; margin-bottom: 24px; word-break: break-all; }"
	   << "ul { list-style: none; display: flex; flex-direction: column; gap: 6px; }"
	   << "ul li a { font-size: 14px; color: #6c7be8; text-decoration: none;"
	   << "  display: block; padding: 8px 12px; border-radius: 8px;"
	   << "  transition: background 0.15s; }"
	   << "ul li a:hover { background: #f0f3ff; }"
	   << ".empty { font-size: 14px; color: #9ca3af; }"
	   << ".divider { border: none; border-top: 1px solid #ebebeb; margin: 24px 0; }"
	   << ".back { font-size: 13px; color: #6c7be8; text-decoration: none; }"
	   << ".back:hover { text-decoration: underline; }"
	   << "</style></head><body><div class=\"card\">"
	   << "<div class=\"label\">http-server</div>"
	   << "<h1>Index of " << uriPath << "</h1>";

	if (entries.empty()) {
		ss << "<p class=\"empty\">No files here yet.</p>";
	} else {
		ss << "<ul>";
		for (size_t i = 0; i < entries.size(); ++i) {
			std::string escaped = escapeHTML(entries[i]);
			ss << "<li><a href=\"" << escaped << "\">" << escaped << "</a></li>";
		}
		ss << "</ul>";
	}

	ss << "<hr class=\"divider\">"
	   << "<a class=\"back\" href=\"/\">Back to home</a>"
	   << " &nbsp;·&nbsp; "
	   << "<a class=\"back\" href=\"/upload.html\">Upload a file</a>"
	   << "</div></body></html>";

	Response res;
	res.statusCode = 200;
	res.statusMessage = "OK";
	res.body = ss.str();
	res.headers["Content-Type"] = "text/html";
	return res;
}

// Creats page with an error
Response RequestHandler::makeErrorResponse(int code, const std::string& msg, const ServerConfig* srv) {
	if (srv != NULL) {
		std::map<int, std::string>::const_iterator it = srv->errorPages.find(code);
		if (it != srv->errorPages.end())
			return loadErrorPage(code, msg, *srv);
	}

	Response res;
	res.statusCode = code;
	res.statusMessage = msg;

	std::stringstream ss;
	ss << "<html><head><title>" << code << " " << msg << "</title></head>";
	ss << "<body><center><h1>" << code << " " << msg << "</h1></center>";
	ss << "<hr><center>http-server/1.0</center></body></html>";

	res.body = ss.str();
	res.headers["Content-Type"] = "text/html";

	return res;
}

// Custom error page from ServerConfig.errorPages
Response RequestHandler::loadErrorPage(int code, const std::string& msg, const ServerConfig& srv) {
	std::map<int, std::string>::const_iterator it = srv.errorPages.find(code);
	if (it == srv.errorPages.end())
		return makeErrorResponse(code, msg, NULL);

	Response page = serveStatic(it->second);
	if (page.statusCode != 200) {
		// Custom page couldn't be read —> fall back to inline
		return makeErrorResponse(code, msg, NULL);
	}
	page.statusCode = code;
	page.statusMessage = msg;
	return page;
}

std::string RequestHandler::sanitizeFilename(const std::string& name) {
	std::string result;
	for (size_t i = 0; i < name.size(); ++i) {
		char c = name[i];
		if (c == '/' || c == '\\' || c == '\0')
			continue;
		if (c == '.' && !result.empty() && result[result.size() - 1] == '.')
			continue; // collapse ".."
		result += c;
	}
	// Delete leading dots to avoid hidden files
	size_t start = result.find_first_not_of('.');
	if (start == std::string::npos)
		return "upload";
	return result.substr(start);
}

bool RequestHandler::saveFile(const std::string& dir, const std::string& name,
								const std::string& data)
{
	std::string path = dir;
	if (!path.empty() && path[path.size() - 1] != '/')
		path += '/';
	path += name;

	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1)
		return false;

	const char* ptr = data.c_str();
	size_t remaining = data.size();
	while (remaining > 0) {
		ssize_t written = write(fd, ptr, remaining);
		if (written <= 0) { close(fd); return false; }
		ptr += written;
		remaining -= static_cast<size_t>(written);
	}
	close(fd);
	return true;
}

// Handle POST upload — multipart/form-data or raw body.
Response RequestHandler::handleUpload(const Request& req, const RouteConfig& route,
										const ServerConfig* srv)
{
	std::string uploadDir = route.uploadDir;
	if (uploadDir.empty())
		return makeErrorResponse(500, "Upload directory not configured", srv);

	const std::string& body = req.getBody();
	std::string contentType = req.getHeader("content-type");

	// Multipart/form-data
	const std::string multipartPrefix = "multipart/form-data";
	if (contentType.substr(0, multipartPrefix.size()) == multipartPrefix) {
		// Extract boundary value from "multipart/form-data; boundary=<value>"
		size_t bPos = contentType.find("boundary=");
		if (bPos == std::string::npos)
			return makeErrorResponse(400, "Missing multipart boundary", srv);
		std::string boundary = "--" + contentType.substr(bPos + 9);
		// Strip optional quotes
		if (!boundary.empty() && boundary[2] == '"') {
			boundary = "--" + contentType.substr(bPos + 10,
							contentType.find('"', bPos + 10) - (bPos + 10));
		}

		std::string terminator = boundary + "--";
		size_t pos = 0;
		bool saved = false;

		while (true) {
			// Find boundary line
			size_t start = body.find(boundary, pos);
			if (start == std::string::npos) break;
			start += boundary.size();

			// Skip CRLF after boundary
			if (start + 1 < body.size() && body[start] == '\r') start += 2;
			else if (start < body.size() && body[start] == '\n') start += 1;
			else break; // "--" suffix -> end

			// Find end of part (next boundary)
			size_t end = body.find("\r\n" + boundary, start);
			if (end == std::string::npos) end = body.find("\n" + boundary, start);
			if (end == std::string::npos) break;

			// Parse part headers
			size_t hdrEnd = body.find("\r\n\r\n", start);
			size_t hdrEndLen = 4;
			if (hdrEnd == std::string::npos || hdrEnd > end) {
				hdrEnd = body.find("\n\n", start);
				hdrEndLen = 2;
			}
			if (hdrEnd == std::string::npos || hdrEnd > end) {
				pos = end + 2;
				continue;
			}

			std::string partHeaders = body.substr(start, hdrEnd - start);
			std::string partBody = body.substr(hdrEnd + hdrEndLen, end - (hdrEnd + hdrEndLen));

			// Extract filename from Content-Disposition header
			std::string filename;
			size_t cdPos = partHeaders.find("Content-Disposition:");
			if (cdPos == std::string::npos)
				cdPos = partHeaders.find("content-disposition:");
			if (cdPos != std::string::npos) {
				size_t fnPos = partHeaders.find("filename=\"", cdPos);
				if (fnPos != std::string::npos) {
					fnPos += 10;
					size_t fnEnd = partHeaders.find('"', fnPos);
					if (fnEnd != std::string::npos)
						filename = partHeaders.substr(fnPos, fnEnd - fnPos);
				}
			}
			if (filename.empty()) {
				pos = end + 2;
				continue; // Skip non-file fields
			}

			filename = sanitizeFilename(filename);
			if (!saveFile(uploadDir, filename, partBody))
				return makeErrorResponse(500, "Failed to save uploaded file", srv);
			saved = true;
			pos = end + 2;
		}

		if (!saved)
			return makeErrorResponse(400, "No file found in upload", srv);

		Response res;
		res.statusCode = 201;
		res.statusMessage = "Created";
		res.headers["Content-Type"] = "text/plain";
		res.body = "Upload successful.\n";
		return res;
	}

	// Raw body upload
	// Use the last segment of the request path as the filename
	std::string rawPath = req.getPath();
	size_t qpos = rawPath.find('?');
	if (qpos != std::string::npos) rawPath = rawPath.substr(0, qpos);
	size_t slash = rawPath.find_last_of('/');
	std::string filename = (slash != std::string::npos) ? rawPath.substr(slash + 1) : rawPath;
	filename = sanitizeFilename(filename);
	if (filename.empty()) filename = "upload";

	if (!saveFile(uploadDir, filename, body))
		return makeErrorResponse(500, "Failed to save uploaded file", srv);

	Response res;
	res.statusCode = 201;
	res.statusMessage = "Created";
	res.headers["Content-Type"] = "text/plain";
	res.body = "Upload successful.\n";
	return res;
}

bool RequestHandler::hasPathTraversal(const std::string& path) {
	// We split the path by / and check each segment
	std::string segment;
	for (size_t i = 0; i <= path.size(); ++i) {
		if (i == path.size() || path[i] == '/') {
			if (segment == "..")
				return true;
			segment.clear();
		} else {
			segment += path[i];
		}
	}
	return false;
}
