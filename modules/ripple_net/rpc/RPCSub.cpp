//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (RPCSub)

// Subscription object for JSON-RPC
class RPCSubImp
    : public RPCSub
    , public LeakChecked <RPCSub>
{
public:
    RPCSubImp (InfoSub::Source& source, boost::asio::io_service& io_service,
        JobQueue& jobQueue, const std::string& strUrl, const std::string& strUsername,
            const std::string& strPassword)
        : RPCSub (source)
        , m_io_service (io_service)
        , m_jobQueue (jobQueue)
        , mUrl (strUrl)
        , mSSL (false)
        , mUsername (strUsername)
        , mPassword (strPassword)
        , mSending (false)
    {
        std::string strScheme;

        if (!parseUrl (strUrl, strScheme, mIp, mPort, mPath))
        {
            throw std::runtime_error ("Failed to parse url.");
        }
        else if (strScheme == "https")
        {
            mSSL    = true;
        }
        else if (strScheme != "http")
        {
            throw std::runtime_error ("Only http and https is supported.");
        }

        mSeq    = 1;

        if (mPort < 0)
            mPort   = mSSL ? 443 : 80;

        WriteLog (lsINFO, RPCSub) <<
            "RPCCall::fromNetwork sub: ip=" << mIp <<
            " port=" << mPort <<
            " ssl= "<< (mSSL ? "yes" : "no") <<
            " path='" << mPath << "'";
    }

    ~RPCSubImp ()
    {
    }

    void send (const Json::Value& jvObj, bool broadcast)
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);

        if (mDeque.size () >= eventQueueMax)
        {
            // Drop the previous event.
            WriteLog (lsWARNING, RPCSub) << "RPCCall::fromNetwork drop";
            mDeque.pop_back ();
        }

        WriteLog (broadcast ? lsDEBUG : lsINFO, RPCSub) <<
            "RPCCall::fromNetwork push: " << jvObj;

        mDeque.push_back (std::make_pair (mSeq++, jvObj));

        if (!mSending)
        {
            // Start a sending thread.
            mSending    = true;

            WriteLog (lsINFO, RPCSub) << "RPCCall::fromNetwork start";

            m_jobQueue.addJob (
                jtCLIENT, "RPCSub::sendThread", BIND_TYPE (&RPCSubImp::sendThread, this));
        }
    }

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

private:
    // XXX Could probably create a bunch of send jobs in a single get of the lock.
    void sendThread ()
    {
        Json::Value jvEvent;
        bool bSend;

        do
        {
            {
                // Obtain the lock to manipulate the queue and change sending.
                ScopedLockType sl (mLock, __FILE__, __LINE__);

                if (mDeque.empty ())
                {
                    mSending    = false;
                    bSend       = false;
                }
                else
                {
                    std::pair<int, Json::Value> pEvent  = mDeque.front ();

                    mDeque.pop_front ();

                    jvEvent     = pEvent.second;
                    jvEvent["seq"]  = pEvent.first;

                    bSend       = true;
                }
            }

            // Send outside of the lock.
            if (bSend)
            {
                // XXX Might not need this in a try.
                try
                {
                    WriteLog (lsINFO, RPCSub) << "RPCCall::fromNetwork: " << mIp;

                    RPCCall::fromNetwork (
                        m_io_service,
                        mIp, mPort,
                        mUsername, mPassword,
                        mPath, "event",
                        jvEvent,
                        mSSL);
                }
                catch (const std::exception& e)
                {
                    WriteLog (lsINFO, RPCSub) << "RPCCall::fromNetwork exception: " << e.what ();
                }
            }
        }
        while (bSend);
    }

private:
// VFALCO TODO replace this macro with a language constant
    enum
    {
        eventQueueMax = 32
    };

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

//------------------------------------------------------------------------------

RPCSub::RPCSub (InfoSub::Source& source)
    : InfoSub (source)
{
}

RPCSub::pointer RPCSub::New (InfoSub::Source& source,
    boost::asio::io_service& io_service, JobQueue& jobQueue,
        const std::string& strUrl, const std::string& strUsername,
        const std::string& strPassword)
{
    return boost::make_shared <RPCSubImp> (boost::ref (source),
        boost::ref (io_service), boost::ref (jobQueue),
            strUrl, strUsername, strPassword);
}