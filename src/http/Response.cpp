#include "http/Response.hpp"
#include <sstream>
#include <ctime>

Response::Response() : statusCode(200), statusMessage("OK") {}

std::string Response::toString() {
	std::stringstream ss;

	ss << "HTTP/1.1 " << statusCode << " " << statusMessage << "\r\n";

	std::stringstream len;
	len << body.length();
	headers["Content-Length"] = len.str();
	headers["Date"] = getDateHeader();
	headers["Server"] = "http-server/1.0";

	std::map<std::string, std::string>::const_iterator it;
	for (it = headers.begin(); it != headers.end(); ++it)
		ss << it->first << ": " << it->second << "\r\n";

	ss << "\r\n";
	ss << body;
	return ss.str();
}

// RFC 7231 date format: "Sat, 01 Jan 2000 00:00:00 GMT"
std::string Response::getDateHeader() {
	char buf[64];
	std::time_t now = std::time(NULL);
	struct tm *gmt = std::gmtime(&now);
	std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", gmt);
	return buf;
}

std::string Response::getReasonPhrase(int code) {
	switch (code) {
		case 200: return "OK";
		case 201: return "Created";
		case 204: return "No Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 400: return "Bad Request";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 409: return "Conflict";
		case 413: return "Payload Too Large";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 504: return "Gateway Timeout";
		default:  return "Unknown";
	}
}

std::string Response::getMimeType(const std::string &path) {
	size_t dot = path.find_last_of('.');
	if (dot == std::string::npos)
		return "application/octet-stream";

	std::string ext = path.substr(dot);
	if (ext == ".html" || ext == ".htm") return "text/html";
	if (ext == ".css") return "text/css";
	if (ext == ".js") return "application/javascript";
	if (ext == ".json") return "application/json";
	if (ext == ".xml") return "application/xml";
	if (ext == ".txt") return "text/plain";
	if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
	if (ext == ".png") return "image/png";
	if (ext == ".gif") return "image/gif";
	if (ext == ".svg") return "image/svg+xml";
	if (ext == ".ico") return "image/x-icon";
	if (ext == ".pdf") return "application/pdf";
	return "application/octet-stream";
}
