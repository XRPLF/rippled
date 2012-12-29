#include "RPCSub.h"

RPCSub::RPCSub(const std::string& strUrl, const std::string& strUsername, const std::string& strPassword)
    : mUrl(strUrl), mUsername(strUsername), mPassword(strPassword)
{

}

void RPCSub::send(const Json::Value& jvObj)
{

}
