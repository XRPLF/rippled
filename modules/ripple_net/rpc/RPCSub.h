//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NET_RPC_RPCSUB_H_INCLUDED
#define RIPPLE_NET_RPC_RPCSUB_H_INCLUDED

/** Subscription object for JSON RPC.
*/
class RPCSub : public InfoSub
{
public:
    typedef boost::shared_ptr <RPCSub> pointer;
    typedef pointer const& ref;

    static pointer New (InfoSub::Source& source,
        boost::asio::io_service& io_service, JobQueue& jobQueue,
            const std::string& strUrl, const std::string& strUsername,
            const std::string& strPassword);

    virtual void setUsername (const std::string& strUsername) = 0;
    virtual void setPassword (const std::string& strPassword) = 0;

protected:
    explicit RPCSub (InfoSub::Source& source);
};

#endif
