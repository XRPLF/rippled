#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <string>
#include <vector>

struct HttpHeader
{
	std::string name;
	std::string value;
};

/// A request received from a client.
struct HttpRequest
{
	std::string method;
	std::string uri;
	int http_version_major;
	int http_version_minor;
	std::vector<HttpHeader> headers;
	std::string mBody;
};

#endif // HTTP_REQUEST_HPP