#ifndef __RPCSUB__
#define __RPCSUB__

#include <deque>

#include "NetworkOPs.h"

#define RPC_EVENT_QUEUE_MAX	32

// Subscription object for JSON-RPC
class RPCSub : public InfoSub
{
	std::string				mUrl;
	std::string				mIp;
	int						mPort;
	bool					mSSL;
	std::string				mUsername;
	std::string				mPassword;
	std::string				mPath;

	int						mSeq;						// Next id to allocate.

	bool					mSending;					// Sending threead is active.

	std::deque<std::pair<int, Json::Value> >	mDeque;

protected:
	void	sendThread();

public:
	typedef boost::shared_ptr<RPCSub>	pointer;
	typedef const pointer&				ref;

	RPCSub(const std::string& strUrl, const std::string& strUsername, const std::string& strPassword);

	virtual ~RPCSub() { ; }

	// Implement overridden functions from base class:
	void send(const Json::Value& jvObj, bool broadcast);

	void setUsername(const std::string& strUsername)
	{
		boost::mutex::scoped_lock sl(mLockInfo);

		mUsername = strUsername;
	}

	void setPassword(const std::string& strPassword)
	{
		boost::mutex::scoped_lock sl(mLockInfo);

		mPassword = strPassword;
	}
};

#endif
// vim:ts=4
