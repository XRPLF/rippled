//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef __RPCSUB__
#define __RPCSUB__

#define RPC_EVENT_QUEUE_MAX 32

// Subscription object for JSON-RPC
class RPCSub
    : public InfoSub
    , LeakChecked <RPCSub>
{
public:
    typedef boost::shared_ptr<RPCSub>   pointer;
    typedef const pointer&              ref;

    RPCSub (boost::asio::io_service& io_service,
        JobQueue& jobQueue, const std::string& strUrl,
            const std::string& strUsername, const std::string& strPassword);

    virtual ~RPCSub ()
    {
        ;
    }

    // Implement overridden functions from base class:
    void send (const Json::Value& jvObj, bool broadcast);

    void setUsername (const std::string& strUsername)
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        mUsername = strUsername;
    }

    void setPassword (const std::string& strPassword)
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        mPassword = strPassword;
    }

protected:
    void    sendThread ();

private:
    boost::asio::io_service& m_io_service;
    JobQueue& m_jobQueue;

    std::string             mUrl;
    std::string             mIp;
    int                     mPort;
    bool                    mSSL;
    std::string             mUsername;
    std::string             mPassword;
    std::string             mPath;

    int                     mSeq;                       // Next id to allocate.

    bool                    mSending;                   // Sending threead is active.

    std::deque<std::pair<int, Json::Value> >    mDeque;
};

#endif
// vim:ts=4
