#include <boost/thread.hpp>

#include "RPCSub.h"

#include "CallRPC.h"

RPCSub::RPCSub(const std::string& strUrl, const std::string& strUsername, const std::string& strPassword)
    : mUrl(strUrl), mUsername(strUsername), mPassword(strPassword)
{
    mId	= 1;
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
		jvEvent["id"]	= pEvent.first;

		bSend		= true;
	    }
	}

	// Send outside of the lock.
	if (bSend)
	{
	    // Drop result.
	    (void) callRPC(mIp, mPort, mUsername, mPassword, "event", jvEvent);

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

    mDeque.push_back(std::make_pair(mId++, jvObj));

    if (!mSending)
    {
	// Start a sending thread.
	mSending    = true;

	boost::thread(boost::bind(&RPCSub::sendThread, this)).detach();
    }
}
