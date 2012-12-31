#include <boost/thread.hpp>

#include "RPCSub.h"

#include "CallRPC.h"

SETUP_LOG();

RPCSub::RPCSub(const std::string& strUrl, const std::string& strUsername, const std::string& strPassword)
    : mUrl(strUrl), mUsername(strUsername), mPassword(strPassword)
{
    std::string	strScheme;

    if (!parseUrl(strUrl, strScheme, mIp, mPort, mPath))
    {
	throw std::runtime_error("Failed to parse url.");
    }
    else if (strScheme != "http")
    {
	throw std::runtime_error("Only http is supported.");
    }

    mSeq	= 1;
}

void RPCSub::sendThread()
{
    Json::Value	jvEvent;
    bool	bSend;

    do
    {
	{
	    // Obtain the lock to manipulate the queue and change sending.
	    boost::mutex::scoped_lock sl(mLockInfo);

	    if (mDeque.empty())
	    {
		mSending	= false;
		bSend		= false;
	    }
	    else
	    {
		std::pair<int, Json::Value> pEvent  = mDeque.front();

		mDeque.pop_front();

		jvEvent		= pEvent.second;
		jvEvent["seq"]	= pEvent.first;

		bSend		= true;
	    }
	}

	// Send outside of the lock.
	if (bSend)
	{
	    // Drop result.
	    try
	    {
		(void) callRPC(mIp, mPort, mUsername, mPassword, mPath, "event", jvEvent);
	    }
	    catch (const std::exception& e)
	    {
		cLog(lsDEBUG) << boost::str(boost::format("callRPC exception: %s") % e.what());
	    }

	    sendThread();
	}
    } while (bSend);
}

void RPCSub::send(const Json::Value& jvObj)
{
    boost::mutex::scoped_lock sl(mLockInfo);

    if (RPC_EVENT_QUEUE_MAX == mDeque.size())
    {
	// Drop the previous event.

	mDeque.pop_back();
    }

    mDeque.push_back(std::make_pair(mSeq++, jvObj));

    if (!mSending)
    {
	// Start a sending thread.
	mSending    = true;

	boost::thread(boost::bind(&RPCSub::sendThread, this)).detach();
    }
}
