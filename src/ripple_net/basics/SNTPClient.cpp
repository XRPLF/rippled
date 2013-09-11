//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (SNTPClient)

// #define SNTP_DEBUG

static uint8_t SNTPQueryData[48] =
{ 0x1B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// NTP query frequency - 4 minutes
#define NTP_QUERY_FREQUENCY     (4 * 60)

// NTP minimum interval to query same servers - 3 minutes
#define NTP_MIN_QUERY           (3 * 60)

// NTP sample window (should be odd)
#define NTP_SAMPLE_WINDOW       9

// NTP timestamp constant
#define NTP_UNIX_OFFSET         0x83AA7E80

// NTP timestamp validity
#define NTP_TIMESTAMP_VALID     ((NTP_QUERY_FREQUENCY + NTP_MIN_QUERY) * 2)

// SNTP packet offsets
#define NTP_OFF_INFO            0
#define NTP_OFF_ROOTDELAY       1
#define NTP_OFF_ROOTDISP        2
#define NTP_OFF_REFERENCEID     3
#define NTP_OFF_REFTS_INT       4
#define NTP_OFF_REFTS_FRAC      5
#define NTP_OFF_ORGTS_INT       6
#define NTP_OFF_ORGTS_FRAC      7
#define NTP_OFF_RECVTS_INT      8
#define NTP_OFF_RECVTS_FRAC     9
#define NTP_OFF_XMITTS_INT      10
#define NTP_OFF_XMITTS_FRAC     11


SNTPClient::SNTPClient (boost::asio::io_service& service)
    : mLock (this, "SNTPClient", __FILE__, __LINE__)
    , mSocket (service)
    , mTimer (service)
    , mResolver (service)
    , mOffset (0)
    , mLastOffsetUpdate ((time_t) - 1)
    , mReceiveBuffer (256)
{
    mSocket.open (boost::asio::ip::udp::v4 ());
    mSocket.async_receive_from (boost::asio::buffer (mReceiveBuffer, 256), mReceiveEndpoint,
                                boost::bind (&SNTPClient::receivePacket, this, boost::asio::placeholders::error,
                                        boost::asio::placeholders::bytes_transferred));

    mTimer.expires_from_now (boost::posix_time::seconds (NTP_QUERY_FREQUENCY));
    mTimer.async_wait (boost::bind (&SNTPClient::timerEntry, this, boost::asio::placeholders::error));
}

void SNTPClient::resolveComplete (const boost::system::error_code& error, boost::asio::ip::udp::resolver::iterator it)
{
    if (!error)
    {
        boost::asio::ip::udp::resolver::iterator sel = it;
        int i = 1;

        while (++it != boost::asio::ip::udp::resolver::iterator ())
            if ((rand () % ++i) == 0)
                sel = it;

        if (sel != boost::asio::ip::udp::resolver::iterator ())
        {
            ScopedLockType sl (mLock, __FILE__, __LINE__);
            SNTPQuery& query = mQueries[*sel];
            time_t now = time (NULL);

            if ((query.mLocalTimeSent == now) || ((query.mLocalTimeSent + 1) == now))
            {
                // This can happen if the same IP address is reached through multiple names
                WriteLog (lsTRACE, SNTPClient) << "SNTP: Redundant query suppressed";
                return;
            }

            query.mReceivedReply = false;
            query.mLocalTimeSent = now;
            RandomNumbers::getInstance ().fill (&query.mQueryNonce);
            reinterpret_cast<uint32*> (SNTPQueryData)[NTP_OFF_XMITTS_INT] = static_cast<uint32> (time (NULL)) + NTP_UNIX_OFFSET;
            reinterpret_cast<uint32*> (SNTPQueryData)[NTP_OFF_XMITTS_FRAC] = query.mQueryNonce;
            mSocket.async_send_to (boost::asio::buffer (SNTPQueryData, 48), *sel,
                                   boost::bind (&SNTPClient::sendComplete, this,
                                                boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
        }
    }
}

void SNTPClient::receivePacket (const boost::system::error_code& error, std::size_t bytes_xferd)
{
    if (!error)
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
#ifdef SNTP_DEBUG
        WriteLog (lsTRACE, SNTPClient) << "SNTP: Packet from " << mReceiveEndpoint;
#endif
        std::map<boost::asio::ip::udp::endpoint, SNTPQuery>::iterator query = mQueries.find (mReceiveEndpoint);

        if (query == mQueries.end ())
            WriteLog (lsDEBUG, SNTPClient) << "SNTP: Reply from " << mReceiveEndpoint << " found without matching query";
        else if (query->second.mReceivedReply)
            WriteLog (lsDEBUG, SNTPClient) << "SNTP: Duplicate response from " << mReceiveEndpoint;
        else
        {
            query->second.mReceivedReply = true;

            if (time (NULL) > (query->second.mLocalTimeSent + 1))
                WriteLog (lsWARNING, SNTPClient) << "SNTP: Late response from " << mReceiveEndpoint;
            else if (bytes_xferd < 48)
                WriteLog (lsWARNING, SNTPClient) << "SNTP: Short reply from " << mReceiveEndpoint
                                                 << " (" << bytes_xferd << ") " << mReceiveBuffer.size ();
            else if (reinterpret_cast<uint32*> (&mReceiveBuffer[0])[NTP_OFF_ORGTS_FRAC] != query->second.mQueryNonce)
                WriteLog (lsWARNING, SNTPClient) << "SNTP: Reply from " << mReceiveEndpoint << "had wrong nonce";
            else
                processReply ();
        }
    }

    mSocket.async_receive_from (boost::asio::buffer (mReceiveBuffer, 256), mReceiveEndpoint,
                                boost::bind (&SNTPClient::receivePacket, this, boost::asio::placeholders::error,
                                        boost::asio::placeholders::bytes_transferred));
}

void SNTPClient::sendComplete (const boost::system::error_code& error, std::size_t)
{
    CondLog (error, lsWARNING, SNTPClient) << "SNTP: Send error";
}

void SNTPClient::processReply ()
{
    assert (mReceiveBuffer.size () >= 48);
    uint32* recvBuffer = reinterpret_cast<uint32*> (&mReceiveBuffer.front ());

    unsigned info = ntohl (recvBuffer[NTP_OFF_INFO]);
    int64_t timev = ntohl (recvBuffer[NTP_OFF_RECVTS_INT]);
    unsigned stratum = (info >> 16) & 0xff;

    if ((info >> 30) == 3)
    {
        WriteLog (lsINFO, SNTPClient) << "SNTP: Alarm condition " << mReceiveEndpoint;
        return;
    }

    if ((stratum == 0) || (stratum > 14))
    {
        WriteLog (lsINFO, SNTPClient) << "SNTP: Unreasonable stratum (" << stratum << ") from " << mReceiveEndpoint;
        return;
    }

    int64 now = static_cast<int> (time (NULL));
    timev -= now;
    timev -= NTP_UNIX_OFFSET;

    // add offset to list, replacing oldest one if appropriate
    mOffsetList.push_back (timev);

    if (mOffsetList.size () >= NTP_SAMPLE_WINDOW)
        mOffsetList.pop_front ();

    mLastOffsetUpdate = now;

    // select median time
    std::list<int> offsetList = mOffsetList;
    offsetList.sort ();
    int j = offsetList.size ();
    std::list<int>::iterator it = offsetList.begin ();

    for (int i = 0; i < (j / 2); ++i)
        ++it;

    mOffset = *it;

    if ((j % 2) == 0)
        mOffset = (mOffset + (*--it)) / 2;

    if ((mOffset == -1) || (mOffset == 1)) // small corrections likely do more harm than good
        mOffset = 0;

    CondLog (timev || mOffset, lsTRACE, SNTPClient) << "SNTP: Offset is " << timev << ", new system offset is " << mOffset;
}

void SNTPClient::timerEntry (const boost::system::error_code& error)
{
    if (!error)
    {
        doQuery ();
        mTimer.expires_from_now (boost::posix_time::seconds (NTP_QUERY_FREQUENCY));
        mTimer.async_wait (boost::bind (&SNTPClient::timerEntry, this, boost::asio::placeholders::error));
    }
}

void SNTPClient::addServer (const std::string& server)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    mServers.push_back (std::make_pair (server, (time_t) - 1));
}

void SNTPClient::init (const std::vector<std::string>& servers)
{
    std::vector<std::string>::const_iterator it = servers.begin ();

    if (it == servers.end ())
    {
        WriteLog (lsINFO, SNTPClient) << "SNTP: no server specified";
        return;
    }

    BOOST_FOREACH (const std::string & it, servers)
    addServer (it);
    queryAll ();
}

void SNTPClient::queryAll ()
{
    while (doQuery ())
        nothing ();
}

bool SNTPClient::getOffset (int& offset)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if ((mLastOffsetUpdate == (time_t) - 1) || ((mLastOffsetUpdate + NTP_TIMESTAMP_VALID) < time (NULL)))
        return false;

    offset = mOffset;
    return true;
}

bool SNTPClient::doQuery ()
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    std::vector< std::pair<std::string, time_t> >::iterator best = mServers.end ();

    for (std::vector< std::pair<std::string, time_t> >::iterator it = mServers.begin (), end = best;
            it != end; ++it)
        if ((best == end) || (it->second == (time_t) - 1) || (it->second < best->second))
            best = it;

    if (best == mServers.end ())
    {
        WriteLog (lsTRACE, SNTPClient) << "SNTP: No server to query";
        return false;
    }

    time_t now = time (NULL);

    if ((best->second != (time_t) - 1) && ((best->second + NTP_MIN_QUERY) >= now))
    {
        WriteLog (lsTRACE, SNTPClient) << "SNTP: All servers recently queried";
        return false;
    }

    best->second = now;

    boost::asio::ip::udp::resolver::query query (boost::asio::ip::udp::v4 (), best->first, "ntp");
    mResolver.async_resolve (query,
                             boost::bind (&SNTPClient::resolveComplete, this,
                                          boost::asio::placeholders::error, boost::asio::placeholders::iterator));
#ifdef SNTP_DEBUG
    WriteLog (lsTRACE, SNTPClient) << "SNTP: Resolve pending for " << best->first;
#endif
    return true;
}
// vim:ts=4
