
#include <string>

#include "../json/value.h"

extern int commandLineRPC(const std::vector<std::string>& vCmd);
extern Json::Value callRPC(const std::string& strMethod, const Json::Value& params);
