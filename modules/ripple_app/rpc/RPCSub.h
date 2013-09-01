//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_RPCSUB_H_INCLUDED
#define RIPPLE_RPCSUB_H_INCLUDED

// VFALCO TODO replace this macro with a language constant
#define RPC_EVENT_QUEUE_MAX 32

// Subscription object for JSON-RPC
// VFALCO TODO Move the implementation into the .cpp
//
class RPCSub
    : public InfoSub
    , public LeakChecked <RPCSub>
{
public:
    typedef boost::shared_ptr<RPCSub>   pointer;
    typedef const pointer&              ref;

    RPCSub (InfoSub::Source& source, boost::asio::io_service& io_service,
        JobQueue& jobQueue, const std::string& strUrl,
            const std::string& strUsername, const std::string& strPassword);

    virtual ~RPCSub () { }

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
