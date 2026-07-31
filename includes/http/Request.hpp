#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <string>
#include <map>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <cctype>

class Request
{
	public:
		enum ParsingStatus
		{
			REQUEST_LINE,
			HEADERS,
			BODY,
			COMPLETE,
			ERROR
		};

		Request();
		~Request();

		void parse(const std::string& rawData);
		void setMaxBodySize(size_t size);
		size_t getMaxBodySize() const;

		const std::string& getMethod() const;
		const std::string& getPath() const;
		const std::string& getVersion() const;
		const std::string& getBody() const;
		void releaseBody(); // free body memory once handed off (e.g. to CGI)
		int getErrorCode() const;
		const std::string& getErrorMessage() const;
		std::string getHeader(const std::string& name) const;
		const std::map<std::string, std::string>& getHeaders() const;
		ParsingStatus getStatus() const;

		std::string rawBuffer; // Stores everything that comes from the network

		private:
		static const size_t DEFAULT_MAX_BODY_SIZE;
		static const size_t MAX_LINE_LENGTH;
		std::string method;
		std::string path;
		std::string version;
		std::string body;
		std::map<std::string, std::string> headers; // Key, value, for HTTP headers, "Content-Type"	"application/json"
		size_t maxBodySize;

		int errorCode;
		std::string errorMsg;

		ParsingStatus status;
		size_t chunkSize;
		bool chunkSizeRead;
		bool inFinalChunk; // True when the zero-length terminal chunk size has been read

		void parseRequestLine();
		void parseHeaders();
		void parseHeader(const std::string& line);
		void parseBody();
		void parseChunkedBody();
		void parseLengthBody();
		void setErrorCode(int code);
};

#endif
