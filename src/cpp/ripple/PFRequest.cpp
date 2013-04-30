#include "PFRequest.h"

#include "NetworkOPs.h"

PFRequest::PFRequest(const boost::shared_ptr<InfoSub>& subscriber, Json::Value request) :
	wpSubscriber(subscriber), jvStatus(Json::objectValue), bValid(false)
{
	if (parseJson(request) == PFR_PJ_COMPLETE)
		bValid = true;
}

bool PFRequest::isValid()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return bValid;
}

int PFRequest::parseJson(const Json::Value& jvParams)
{
	return 0;
}
