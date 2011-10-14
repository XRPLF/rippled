#include "json/json_spirit_value.h"

enum http_status_type
{
	ok = 200,
	created = 201,
	accepted = 202,
	no_content = 204,
	multiple_choices = 300,
	moved_permanently = 301,
	moved_temporarily = 302,
	not_modified = 304,
	bad_request = 400,
	unauthorized = 401,
	forbidden = 403,
	not_found = 404,
	internal_server_error = 500,
	not_implemented = 501,
	bad_gateway = 502,
	service_unavailable = 503
};

extern std::string JSONRPCRequest(const std::string& strMethod, const json_spirit::Array& params, const json_spirit::Value& id);
extern std::string createHTTPPost(const std::string& strMsg, const std::map<std::string,std::string>& mapRequestHeaders);
extern int ReadHTTP(std::basic_istream<char>& stream, std::map<std::string, std::string>& mapHeadersRet, std::string& strMessageRet);
extern std::string HTTPReply(int nStatus, const std::string& strMsg);
extern std::string JSONRPCReply(const json_spirit::Value& result, const json_spirit::Value& error, const json_spirit::Value& id);
extern json_spirit::Object JSONRPCError(int code, const std::string& message);