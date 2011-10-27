#include "uint256.h"
#include <string>

extern uint160 protobufTo160(const std::string& buf);
extern uint256 protobufTo256(const std::string& hash);
extern uint160 humanTo160(const std::string& buf);
extern bool humanToPK(const std::string& buf,std::vector<unsigned char>& retVec);


extern bool u160ToHuman(uint160& buf, std::string& retStr);

