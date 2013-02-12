#include <boost/thread.hpp>

#include "Application.h"
#include "RPCSub.h"

#include "CallRPC.h"

SETUP_LOG();

RPCSub::RPCSub(const std::string& strUrl, const std::string& strUsername, const std::string& strPassword)
    : mUrl(strUrl), mSSL(false), mUsername(strUsername), mPassword(strPassword), mSending(false)
{
    std::string	strScheme;

    if (!parseUrl(strUrl, strScheme, mIp, mPort, mPath))
    {
		throw std::runtime_error("Failed to parse url.");
    }
    else if (strScheme == "https")
    {
		mSSL	= true;
    }
    else if (strScheme != "http")
    {
		throw std::runtime_error("Only http and https is supported.");
    }

    mSeq	= 1;

    if (mPort < 0)
		mPort	= mSSL ? 443 : 80;
}

// XXX Could probably create a bunch of send jobs in a single get of the lock.
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
			// XXX Might not need this in a try.
			try
			{
				cLog(lsINFO) << boost::str(boost::format("callRPC calling: %s") % mIp);

				callRPC(
					theApp->getIOService(),
					mIp, mPort,
					mUsername, mPassword,
					mPath, "event",
					jvEvent,
					mSSL);
			}
			catch (const std::exception& e)
			{
				cLog(lsINFO) << boost::str(boost::format("callRPC exception: %s") % e.what());
			}
		}
    } while (bSend);
}

void RPCSub::send(const Json::Value& jvObj)
{
    boost::mutex::scoped_lock sl(mLockInfo);

    if (RPC_EVENT_QUEUE_MAX == mDeque.size())
    {
	// Drop the previous event.
		cLog(lsWARNING) << boost::str(boost::format("callRPC drop"));
		mDeque.pop_back();
    }

    cLog(lsINFO) << boost::str(boost::format("callRPC push: %s") % jvObj);

    mDeque.push_back(std::make_pair(mSeq++, jvObj));

    if (!mSending)
    {
		// Start a sending thread.
		mSending    = true;

	    cLog(lsINFO) << boost::str(boost::format("callRPC start"));
		boost::thread(boost::bind(&RPCSub::sendThread, this)).detach();
    }
}
