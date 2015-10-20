//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/ThreadName.h>
#include <ripple/crypto/RandomNumbers.h>
#include <ripple/core/impl/SNTPClock.h>
#include <beast/asio/placeholders.h>
#include <beast/threads/Thread.h>
#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace ripple {

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

class SNTPClientImp
    : public SNTPClock
{
private:
    struct Query
    {
        bool replied;
        time_t sent; // VFALCO time_t, really?
        std::uint32_t nonce;

        Query (time_t j = time_t(-1))
            : replied (false)
            , sent (j)
        {
        }
    };

    beast::Journal j_;
    std::mutex mutable mutex_;
    std::thread thread_;
    boost::asio::io_service io_service_;
    boost::optional<
        boost::asio::io_service::work> work_;

    std::map <boost::asio::ip::udp::endpoint, Query> queries_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::deadline_timer timer_;
    boost::asio::ip::udp::resolver resolver_;
    std::vector<std::pair<std::string, time_t>> servers_;
    int offset_;
    time_t lastUpdate_;
    std::deque<int> offsets_;
    std::vector<uint8_t> buf_;
    boost::asio::ip::udp::endpoint ep_;

public:
    using error_code = boost::system::error_code;

    explicit
    SNTPClientImp (beast::Journal j)
        : j_ (j)
        , work_(io_service_)
        , socket_ (io_service_)
        , timer_ (io_service_)
        , resolver_ (io_service_)
        , offset_ (0)
        , lastUpdate_ (time_t(-1))
        , buf_ (256)
    {
    }

    ~SNTPClientImp ()
    {
        if (thread_.joinable())
        {
            error_code ec;
            timer_.cancel(ec);
            socket_.cancel(ec);
            work_ = boost::none;
            thread_.join();
        }
    }

    //--------------------------------------------------------------------------

    void
    run (const std::vector<std::string>& servers) override
    {
        std::vector<std::string>::const_iterator it = servers.begin ();

        if (it == servers.end ())
        {
            JLOG(j_.info) <<
                "SNTP: no server specified";
            return;
        }

        {
            std::lock_guard<std::mutex> lock (mutex_);
            for (auto const& item : servers)
                servers_.emplace_back(
                    item, time_t(-1));
        }
        queryAll();

        using namespace boost::asio;
        socket_.open (ip::udp::v4 ());
        socket_.async_receive_from (buffer (buf_, 256),
            ep_, std::bind(
                &SNTPClientImp::onRead, this,
                    beast::asio::placeholders::error,
                        beast::asio::placeholders::bytes_transferred));
        timer_.expires_from_now(
            boost::posix_time::seconds(NTP_QUERY_FREQUENCY));
        timer_.async_wait(std::bind(
            &SNTPClientImp::onTimer, this,
                beast::asio::placeholders::error));

        // VFALCO Is it correct to launch the thread
        //        here after queuing I/O?
        //
        thread_ = std::thread(&SNTPClientImp::doRun, this);
    }

    time_point
    now() const override
    {
        std::lock_guard<std::mutex> lock (mutex_);
        auto const when = clock_type::now();
        if ((lastUpdate_ == (time_t)-1) ||
                ((lastUpdate_ + NTP_TIMESTAMP_VALID) < time(nullptr)))
            return when;
        return when + std::chrono::seconds(offset_);
    }

    duration
    offset() const override
    {
        std::lock_guard<std::mutex> lock (mutex_);
        return std::chrono::seconds(offset_);
    }

    //--------------------------------------------------------------------------

    void doRun ()
    {
        setCallingThreadName("SNTPClock");
        io_service_.run();
    }

    void
    onTimer (error_code const& ec)
    {
        using namespace boost::asio;
        if (ec == error::operation_aborted)
            return;
        if (ec)
        {
            JLOG(j_.error) <<
                "SNTPClock::onTimer: " << ec.message();
            return;
        }

        doQuery ();
        timer_.expires_from_now(
            boost::posix_time::seconds (NTP_QUERY_FREQUENCY));
        timer_.async_wait(std::bind(
            &SNTPClientImp::onTimer, this,
                beast::asio::placeholders::error));
    }

    void
    onRead (error_code const& ec, std::size_t bytes_xferd)
    {
        using namespace boost::asio;
        if (ec == error::operation_aborted)
            return;

        // VFALCO Should we return on any error?
        /*
        if (ec)
            return;
        */

        if (! ec)
        {
            JLOG(j_.trace) <<
                "SNTP: Packet from " << ep_;
            std::lock_guard<std::mutex> lock (mutex_);
            auto const query = queries_.find (ep_);
            if (query == queries_.end ())
            {
                JLOG(j_.debug) <<
                    "SNTP: Reply from " << ep_ << " found without matching query";
            }
            else if (query->second.replied)
            {
                JLOG(j_.debug) <<
                    "SNTP: Duplicate response from " << ep_;
            }
            else
            {
                query->second.replied = true;

                if (time (nullptr) > (query->second.sent + 1))
                {
                    JLOG(j_.warning) <<
                        "SNTP: Late response from " << ep_;
                }
                else if (bytes_xferd < 48)
                {
                    JLOG(j_.warning) <<
                        "SNTP: Short reply from " << ep_ <<
                            " (" << bytes_xferd << ") " << buf_.size ();
                }
                else if (reinterpret_cast<std::uint32_t*>(
                        &buf_[0])[NTP_OFF_ORGTS_FRAC] !=
                            query->second.nonce)
                {
                    JLOG(j_.warning) <<
                        "SNTP: Reply from " << ep_ << "had wrong nonce";
                }
                else
                {
                    processReply ();
                }
            }
        }

        socket_.async_receive_from(buffer(buf_, 256),
            ep_, std::bind(&SNTPClientImp::onRead, this,
                beast::asio::placeholders::error,
                    beast::asio::placeholders::bytes_transferred));
    }

    //--------------------------------------------------------------------------

    void addServer (std::string const& server)
    {
        std::lock_guard<std::mutex> lock (mutex_);
        servers_.push_back (std::make_pair (server, time_t(-1)));
    }

    void queryAll ()
    {
        while (doQuery ())
        {
        }
    }

    bool doQuery ()
    {
        std::lock_guard<std::mutex> lock (mutex_);
        std::vector< std::pair<std::string, time_t> >::iterator best = servers_.end ();

        for (auto iter = servers_.begin (), end = best;
                iter != end; ++iter)
            if ((best == end) || (iter->second == time_t(-1)) || (iter->second < best->second))
                best = iter;

        if (best == servers_.end ())
        {
            JLOG(j_.trace) <<
                "SNTP: No server to query";
            return false;
        }

        time_t now = time (nullptr);

        if ((best->second != time_t(-1)) && ((best->second + NTP_MIN_QUERY) >= now))
        {
            JLOG(j_.trace) <<
                "SNTP: All servers recently queried";
            return false;
        }

        best->second = now;

        boost::asio::ip::udp::resolver::query query(
            boost::asio::ip::udp::v4 (), best->first, "ntp");
        resolver_.async_resolve (query, std::bind (
            &SNTPClientImp::resolveComplete, this,
                beast::asio::placeholders::error,
                    beast::asio::placeholders::iterator));
        JLOG(j_.trace) <<
            "SNTPClock: Resolve pending for " << best->first;
        return true;
    }

    void resolveComplete (error_code const& ec,
        boost::asio::ip::udp::resolver::iterator it)
    {
        using namespace boost::asio;
        if (ec == error::operation_aborted)
            return;
        if (ec)
        {
            JLOG(j_.trace) <<
                "SNTPClock::resolveComplete: " << ec.message();
            return;
        }

        ip::udp::resolver::iterator sel = it;
        int i = 1;

        while (++it != ip::udp::resolver::iterator())
            if ((rand () % ++i) == 0)
                sel = it;

        if (sel != ip::udp::resolver::iterator ())
        {
            std::lock_guard<std::mutex> lock (mutex_);
            Query& query = queries_[*sel];
            time_t now = time (nullptr);

            if ((query.sent == now) || ((query.sent + 1) == now))
            {
                // This can happen if the same IP address is reached through multiple names
                JLOG(j_.trace) <<
                    "SNTP: Redundant query suppressed";
                return;
            }

            query.replied = false;
            query.sent = now;
            random_fill (&query.nonce);
            reinterpret_cast<std::uint32_t*> (SNTPQueryData)[NTP_OFF_XMITTS_INT] = static_cast<std::uint32_t> (time (nullptr)) + NTP_UNIX_OFFSET;
            reinterpret_cast<std::uint32_t*> (SNTPQueryData)[NTP_OFF_XMITTS_FRAC] = query.nonce;
            socket_.async_send_to(buffer(SNTPQueryData, 48),
                *sel, std::bind (&SNTPClientImp::onSend, this,
                    beast::asio::placeholders::error,
                        beast::asio::placeholders::bytes_transferred));
        }
    }

    void onSend (error_code const& ec, std::size_t)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            JLOG(j_.warning) <<
                "SNTPClock::onSend: " << ec.message();
            return;
        }
    }

    void processReply ()
    {
        assert (buf_.size () >= 48);
        std::uint32_t* recvBuffer = reinterpret_cast<std::uint32_t*> (&buf_.front ());

        unsigned info = ntohl (recvBuffer[NTP_OFF_INFO]);
        int64_t timev = ntohl (recvBuffer[NTP_OFF_RECVTS_INT]);
        unsigned stratum = (info >> 16) & 0xff;

        if ((info >> 30) == 3)
        {
            JLOG(j_.info) <<
                "SNTP: Alarm condition " << ep_;
            return;
        }

        if ((stratum == 0) || (stratum > 14))
        {
            JLOG(j_.info) <<
                "SNTP: Unreasonable stratum (" << stratum << ") from " << ep_;
            return;
        }

        std::int64_t now = static_cast<int> (time (nullptr));
        timev -= now;
        timev -= NTP_UNIX_OFFSET;

        // add offset to list, replacing oldest one if appropriate
        offsets_.push_back (timev);

        if (offsets_.size () >= NTP_SAMPLE_WINDOW)
            offsets_.pop_front ();

        lastUpdate_ = now;

        // select median time
        auto offsetList = offsets_;
        std::sort(offsetList.begin(), offsetList.end());
        auto j = offsetList.size ();
        auto it = std::next(offsetList.begin (), j/2);
        offset_ = *it;

        if ((j % 2) == 0)
            offset_ = (offset_ + (*--it)) / 2;

        // debounce: small corrections likely
        //           do more harm than good
        if ((offset_ == -1) || (offset_ == 1))
            offset_ = 0;

        if (timev || offset_)
        {
            JLOG(j_.trace) << "SNTP: Offset is " << timev <<
                ", new system offset is " << offset_;
        }
    }
};

//------------------------------------------------------------------------------

std::unique_ptr<SNTPClock>
make_SNTPClock (beast::Journal j)
{
    return std::make_unique<SNTPClientImp>(j);
}

} // ripple
