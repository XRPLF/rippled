#include "PFRequest.h"

#include "NetworkOPs.h"
#include "RPCErr.h"

boost::recursive_mutex		PFRequest::sLock;
std::set<PFRequest::wptr>	PFRequest::sRequests;

PFRequest::PFRequest(const boost::shared_ptr<InfoSub>& subscriber) :
	wpSubscriber(subscriber), jvStatus(Json::objectValue), bValid(false)
{
	;
}

bool PFRequest::isValid()
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	bValid = raSrcAccount.isSet() && raDstAccount.isSet() && saDstAmount.isPositive();
	return bValid;
}

Json::Value PFRequest::doCreate(const Json::Value& value)
{
	Json::Value status;
	bool mValid;

	{
		boost::recursive_mutex::scoped_lock sl(mLock);
		parseJson(value, true);
		status = jvStatus;
		mValid = isValid();
	}

	if (mValid)
	{
		boost::recursive_mutex::scoped_lock sl(sLock);
		sRequests.insert(shared_from_this());
	}

	return jvStatus;
}

int PFRequest::parseJson(const Json::Value& jvParams, bool complete)
{
	int ret = PFR_PJ_NOCHANGE;

	if (jvParams.isMember("source_account"))
	{
	}
	else if (complete)
	{
		jvStatus = rpcSRC_ACT_MISSING;
		return PFR_PJ_INVALID;
	}

	if (jvParams.isMember("destination_account"))
	{
	}
	else if (complete)
	{
		jvStatus = rpcDST_ACT_MISSING;
		return PFR_PJ_INVALID;
	}

	if (jvParams.isMember("destination_amount"))
	{
	}
	else if (complete)
	{
	}

	if (jvParams.isMember("source_currencies"))
	{
	}



	return ret;
}

Json::Value PFRequest::doClose(const Json::Value&)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return jvStatus;
}

Json::Value PFRequest::doStatus(const Json::Value&)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	return jvStatus;
}

// vim:ts=4
