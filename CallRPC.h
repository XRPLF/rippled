
#include <string>

#include "json/value.h"

extern int commandLineRPC(int argc, char *argv[]);
extern Json::Value callRPC(const std::string& strMethod, const Json::Value& params);
