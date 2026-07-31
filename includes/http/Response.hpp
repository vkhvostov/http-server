#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#include <string>
#include <map>

struct Response {
	int statusCode;
	std::string statusMessage;
	std::string body;
	std::map<std::string, std::string> headers;

	Response();

	std::string toString();

	static std::string getMimeType(const std::string &path);
	static std::string getReasonPhrase(int code);
	static std::string getDateHeader();
};

#endif
