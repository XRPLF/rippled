//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SNTPCLIENT_H_INCLUDED
#define RIPPLE_SNTPCLIENT_H_INCLUDED

class SNTPQuery
{
public:
    bool                mReceivedReply;
    time_t              mLocalTimeSent;
    uint32              mQueryNonce;

    SNTPQuery (time_t j = (time_t) - 1)   : mReceivedReply (false), mLocalTimeSent (j)
    {
        ;
    }
};

//------------------------------------------------------------------------------

// VFALCO TODO Make an abstract interface for this to hide the boost
//
class SNTPClient : LeakChecked <SNTPClient>
{
public:
    explicit SNTPClient (boost::asio::io_service& service);

    void init (std::vector <std::string> const& servers);

    void addServer (std::string const& mServer);

    void queryAll ();
    bool doQuery ();
    bool getOffset (int& offset);

private:
    void receivePacket (const boost::system::error_code& error, std::size_t bytes);
    void resolveComplete (const boost::system::error_code& error, boost::asio::ip::udp::resolver::iterator iterator);
    void sentPacket (boost::shared_ptr<std::string>, const boost::system::error_code&, std::size_t);
    void timerEntry (const boost::system::error_code&);
    void sendComplete (const boost::system::error_code& error, std::size_t bytesTransferred);
    void processReply ();

private:
    std::map <boost::asio::ip::udp::endpoint, SNTPQuery> mQueries;
    boost::mutex                        mLock;

    boost::asio::ip::udp::socket        mSocket;
    boost::asio::deadline_timer         mTimer;
    boost::asio::ip::udp::resolver      mResolver;

    std::vector< std::pair<std::string, time_t> >   mServers;

    int                                             mOffset;
    time_t                                          mLastOffsetUpdate;
    std::list<int>                                  mOffsetList;

    std::vector<uint8_t>                mReceiveBuffer;
    boost::asio::ip::udp::endpoint      mReceiveEndpoint;
};

#endif
