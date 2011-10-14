#include "json/json_spirit_value.h"

extern int commandLineRPC(int argc, char *argv[]);
extern json_spirit::Object callRPC(const std::string& strMethod, const json_spirit::Array& params);