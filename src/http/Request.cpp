#include "../../includes/http/Request.hpp"

const size_t Request::DEFAULT_MAX_BODY_SIZE = 10485760; // 10MB
const size_t Request::MAX_LINE_LENGTH = 8192; // 8KB

Request::Request()
	: maxBodySize(DEFAULT_MAX_BODY_SIZE),
		errorCode(0),
		status(REQUEST_LINE),
		chunkSize(0),
		chunkSizeRead(false),
		inFinalChunk(false)
{}
Request::~Request() {};

void Request::parseRequestLine()
{
	if (rawBuffer.find("\r\n") == std::string::npos && rawBuffer.size() > MAX_LINE_LENGTH) {
		setErrorCode(414); // URI Too Long
		return;
	}
	size_t pos = rawBuffer.find("\r\n");
	if (pos == std::string::npos)
		return;
	std::string line = rawBuffer.substr(0, pos);
	rawBuffer.erase(0, pos + 2);

	std::stringstream lineStream(line);
	if (!(lineStream >> method >> path >> version)) {
		setErrorCode(400);
		return;
	}
	
	if (method != "GET" && method != "POST" && method != "DELETE" && method != "HEAD") {
		setErrorCode(405);
		return;
	}
	if (path.empty() || path[0] != '/') {
		setErrorCode(400);
		return;
	}
	if (path.size() > MAX_LINE_LENGTH) {
		setErrorCode(414);
		return;
	}
	if (version != "HTTP/1.1") {
		setErrorCode(505);
		return;
	}
	status = HEADERS;
}

void Request::parseBody() {
	// Chunked has priority over Content-Length
	if (getHeader("transfer-encoding") == "chunked") {
		parseChunkedBody();
	} 
	else if (!getHeader("content-length").empty()) {
		parseLengthBody();
	} 
	else {
		// If neither is present, there is no body (ok for GET)
		status = COMPLETE;
	}
}

void Request::parseChunkedBody() {
	while(true) {
		// If we already read a zero-size chunk, consume trailers until the empty line.
		if (inFinalChunk) {
			while (true) {
				size_t nextCrlf = rawBuffer.find("\r\n");
				if (nextCrlf == std::string::npos)
					return;
				rawBuffer.erase(0, nextCrlf + 2);
				if (nextCrlf == 0) {
					status = COMPLETE;
					return;
				}
			}
		}

		if (!chunkSizeRead) {
			size_t pos = rawBuffer.find("\r\n");
			if (pos == std::string::npos) return;

			std::string sizeStr = rawBuffer.substr(0, pos);
			char* endptr;

			chunkSize = std::strtoul(sizeStr.c_str(), &endptr, 16);
			if (sizeStr.empty() || (*endptr != '\0' && *endptr != ';')) {
				setErrorCode(400);
				return;
			}

			chunkSizeRead = true;
			rawBuffer.erase(0, pos + 2);

			if (chunkSize == 0) {
				inFinalChunk = true;
				chunkSizeRead = false;
				continue;
			}
		}

		if (rawBuffer.size() < chunkSize + 2) return;

		if (rawBuffer[chunkSize] != '\r' || rawBuffer[chunkSize + 1] != '\n') {
			setErrorCode(400);
			return;
		}
		body.append(rawBuffer.data(), chunkSize);
		rawBuffer.erase(0, chunkSize + 2);
		chunkSizeRead = false;

		if (body.size() > maxBodySize) {
			setErrorCode(413);
			return;
		}
	}
}

void Request::parseLengthBody() {
	std::string clStr = getHeader("content-length");
	if (clStr.empty()) {
		if (method == "POST") {
			if (getHeader("transfer-encoding") != "chunked") {
				setErrorCode(411); // Length Required
				return;
			}
		}
		status = COMPLETE;
		return;
	}

	if (!clStr.empty() && clStr[0] == '-') {
		setErrorCode(400); // Bad Request
		return;
	}

	char* endptr;
	unsigned long contentLength = std::strtoul(clStr.c_str(), &endptr, 10);
	if (*endptr != '\0') {
		setErrorCode(400); // Bad Request: non-digit symbols
		return;
	}

	if (contentLength > maxBodySize) {
		setErrorCode(413);
		return;
	}
	contentLength = static_cast<size_t>(contentLength);

	// Check if we already have more data than declared
	if (body.size() > contentLength) {
		setErrorCode(400); // Bad Request
		return;
	}
	// How many bytes still need to fill the body
	size_t bytesNeeded = contentLength - body.size();
	// take as much as is actually in the buffer, but no more than necessary
	size_t bytesToTake = std::min(bytesNeeded, rawBuffer.size());

	body.append(rawBuffer.data(), bytesToTake);
	rawBuffer.erase(0, bytesToTake);

	if (body.size() == contentLength) {
		status = COMPLETE;
	} else {
		// Continue waiting for more data in the next poll/select cycle
		status = BODY;
	}
}

void Request::parseHeaders()
{
	size_t pos;

	while ((pos = rawBuffer.find("\r\n")) != std::string::npos) {
	std::string line = rawBuffer.substr(0, pos);
	rawBuffer.erase(0, pos + 2);

	if (line.empty()) {
		if (headers.find("host") == headers.end())
			setErrorCode(400);
		else
			status = BODY;
		return;
	}
		parseHeader(line);
	}
}

void Request::parseHeader(const std::string& line) {
	size_t colonPos = line.find(':');
	if (colonPos == std::string::npos) return;

	std::string key = line.substr(0, colonPos);
	std::string value = line.substr(colonPos + 1);
	for (size_t i = 0; i < key.size(); ++i) {
		key[i] = std::tolower(static_cast<unsigned char>(key[i]));
	}
	
	size_t firstNotSpace = value.find_first_not_of(" \t");
	if (firstNotSpace != std::string::npos) {
		size_t last = value.find_last_not_of(" \t");
		value = value.substr(firstNotSpace, (last - firstNotSpace + 1));
	} else
		value = "";
	headers[key] = value;
}

void Request::setMaxBodySize(size_t size) {
	maxBodySize = size;
}

size_t Request::getMaxBodySize() const {
	return maxBodySize;
}

void Request::parse(const std::string& rawData) {
	rawBuffer += rawData;
	bool progress = true;

	while (progress && status != COMPLETE && status != ERROR) {
		size_t oldPos = rawBuffer.size();

		if (status == REQUEST_LINE)
			parseRequestLine();
		else if (status == HEADERS)
			parseHeaders();
		else if (status == BODY)
			parseBody();
		if (oldPos == rawBuffer.size())
			progress = false;
	}
}

const std::string& Request::getMethod() const
{
	return method;
}
const std::string& Request::getPath() const
{
	return path;
}
const std::string& Request::getVersion() const
{
	return version;
}
const std::string& Request::getBody() const
{
	return body;
}
void Request::releaseBody()
{
	// swap with a temporary so the capacity is actually freed
	std::string().swap(body);
}
int Request::getErrorCode() const
{
	return errorCode;
}

const std::string& Request::getErrorMessage() const
{
	return errorMsg;
}
std::string Request::getHeader(const std::string& name) const
{
	std::string lowercaseName = name;
	for (size_t i = 0; i < lowercaseName.size(); ++i) {
		lowercaseName[i] = std::tolower(static_cast<unsigned char>(lowercaseName[i]));
	}

	std::map<std::string, std::string>::const_iterator it = headers.find(lowercaseName);

	if (it != headers.end()) // Out of map
		return it->second; // First->key, second -> value
	return "";
}

const std::map<std::string, std::string>& Request::getHeaders() const
{
	return headers;
}

Request::ParsingStatus Request::getStatus() const
{
	return status;
}

void Request::setErrorCode(int code) {
	errorCode = code;
	status = ERROR;

	static std::map<int, std::string> messages;

	if (messages.empty()) {
		messages[200] = "OK";
		messages[201] = "Created";
		messages[400] = "Bad Request";
		messages[403] = "Forbidden";
		messages[404] = "Not Found";
		messages[405] = "Method Not Allowed";
		messages[411] = "Length Required";
		messages[413] = "Payload Too Large";
		messages[414] = "URI Too Long";
		messages[500] = "Internal Server Error";
		messages[501] = "Not Implemented";
		messages[505] = "HTTP Version Not Supported";
	}

	if (messages.count(code)) {
		errorMsg = messages[code];
	} else {
		errorMsg = "Unknown Error";
	}
}
