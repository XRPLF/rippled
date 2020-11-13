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

#include <ripple/basics/Log.h>
#include <ripple/basics/random.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/core/impl/SNTPClock.h>
#include <boost/asio.hpp>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace ripple {

static uint8_t SNTPQueryData[48] = {
    0x1B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

using namespace std::chrono_literals;
// NTP query frequency - 4 minutes
auto constexpr NTP_QUERY_FREQUENCY = 4min;

// NTP minimum interval to query same servers - 3 minutes
auto constexpr NTP_MIN_QUERY = 3min;

// NTP sample window (should be odd)
#define NTP_SAMPLE_WINDOW 9

// NTP timestamp constant
auto constexpr NTP_UNIX_OFFSET = 0x83AA7E80s;

// NTP timestamp validity
auto constexpr NTP_TIMESTAMP_VALID = (NTP_QUERY_FREQUENCY + NTP_MIN_QUERY) * 2;

// SNTP packet offsets
#define NTP_OFF_INFO 0
#define NTP_OFF_ROOTDELAY 1
#define NTP_OFF_ROOTDISP 2
#define NTP_OFF_REFERENCEID 3
#define NTP_OFF_REFTS_INT 4
#define NTP_OFF_REFTS_FRAC 5
#define NTP_OFF_ORGTS_INT 6
#define NTP_OFF_ORGTS_FRAC 7
#define NTP_OFF_RECVTS_INT 8
#define NTP_OFF_RECVTS_FRAC 9
#define NTP_OFF_XMITTS_INT 10
#define NTP_OFF_XMITTS_FRAC 11

class SNTPClientImp : public SNTPClock
{
private:
    template <class Duration>
    using sys_time = std::chrono::time_point<clock_type, Duration>;

    using sys_seconds = sys_time<std::chrono::seconds>;

    struct Query
    {
        bool replied;
        sys_seconds sent;
        std::uint32_t nonce;

        explicit Query(sys_seconds j = sys_seconds::max())
            : replied(false), sent(j)
        {
        }
    };

    beast::Journal const j_;
    std::mutex mutable mutex_;
    std::thread thread_;
    boost::asio::io_service io_service_;
    std::optional<boost::asio::io_service::work> work_;

    std::map<boost::asio::ip::udp::endpoint, Query> queries_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::basic_waitable_timer<std::chrono::system_clock> timer_;
    boost::asio::ip::udp::resolver resolver_;
    std::vector<std::pair<std::string, sys_seconds>> servers_;
    std::chrono::seconds offset_;
    sys_seconds lastUpdate_;
    std::deque<std::chrono::seconds> offsets_;
    std::vector<uint8_t> buf_;
    boost::asio::ip::udp::endpoint ep_;

public:
    using error_code = boost::system::error_code;

    explicit SNTPClientImp(beast::Journal j)
        : j_(j)
        , work_(io_service_)
        , socket_(io_service_)
        , timer_(io_service_)
        , resolver_(io_service_)
        , offset_(0)
        , lastUpdate_(sys_seconds::max())
        , buf_(256)
    {
    }

    ~SNTPClientImp() override
    {
        if (thread_.joinable())
        {
            error_code ec;
            timer_.cancel(ec);
            socket_.cancel(ec);
            work_ = std::nullopt;
            thread_.join();
        }
    }

    //--------------------------------------------------------------------------

    void
    run(const std::vector<std::string>& servers) override
    {
        std::vector<std::string>::const_iterator it = servers.begin();

        if (it == servers.end())
        {
            JLOG(j_.info()) << "SNTP: no server specified";
            return;
        }

        {
            std::lock_guard lock(mutex_);
            for (auto const& item : servers)
                servers_.emplace_back(item, sys_seconds::max());
        }
        queryAll();

        using namespace boost::asio;
        socket_.open(ip::udp::v4());
        socket_.bind(ep_);
        socket_.async_receive_from(
            buffer(buf_, 256),
            ep_,
            std::bind(
                &SNTPClientImp::onRead,
                this,
                std::placeholders::_1,
                std::placeholders::_2));
        timer_.expires_from_now(NTP_QUERY_FREQUENCY);
        timer_.async_wait(
            std::bind(&SNTPClientImp::onTimer, this, std::placeholders::_1));

        thread_ = std::thread(&SNTPClientImp::doRun, this);
    }

    time_point
    now() const override
    {
        std::lock_guard lock(mutex_);
        using namespace std::chrono;
        auto const when = time_point_cast<seconds>(clock_type::now());
        if ((lastUpdate_ == sys_seconds::max()) ||
            ((lastUpdate_ + NTP_TIMESTAMP_VALID) <
             time_point_cast<seconds>(clock_type::now())))
            return when;
        return when + offset_;
    }

    duration
    offset() const override
    {
        std::lock_guard lock(mutex_);
        return offset_;
    }

    //--------------------------------------------------------------------------

    void
    doRun()
    {
        beast::setCurrentThreadName("rippled: SNTPClock");
        io_service_.run();
    }

    void
    onTimer(error_code const& ec)
    {
        using namespace boost::asio;
        if (ec == error::operation_aborted)
            return;
        if (ec)
        {
            JLOG(j_.error()) << "SNTPClock::onTimer: " << ec.message();
            return;
        }

        doQuery();
        timer_.expires_from_now(NTP_QUERY_FREQUENCY);
        timer_.async_wait(
            std::bind(&SNTPClientImp::onTimer, this, std::placeholders::_1));
    }

    void
    onRead(error_code const& ec, std::size_t bytes_xferd)
    {
        using namespace boost::asio;
        using namespace std::chrono;
        if (ec == error::operation_aborted)
            return;

        // VFALCO Should we return on any error?
        /*
        if (ec)
            return;
        */

        if (!ec)
        {
            JLOG(j_.trace()) << "SNTP: Packet from " << ep_;
            std::lock_guard lock(mutex_);
            auto const query = queries_.find(ep_);
            if (query == queries_.end())
            {
                JLOG(j_.debug()) << "SNTP: Reply from " << ep_
                                 << " found without matching query";
            }
            else if (query->second.replied)
            {
                JLOG(j_.debug()) << "SNTP: Duplicate response from " << ep_;
            }
            else
            {
                query->second.replied = true;

                if (time_point_cast<seconds>(clock_type::now()) >
                    (query->second.sent + 1s))
                {
                    JLOG(j_.warn()) << "SNTP: Late response from " << ep_;
                }
                else if (bytes_xferd < 48)
                {
                    JLOG(j_.warn()) << "SNTP: Short reply from " << ep_ << " ("
                                    << bytes_xferd << ") " << buf_.size();
                }
                else if (
                    reinterpret_cast<std::uint32_t*>(
                        &buf_[0])[NTP_OFF_ORGTS_FRAC] != query->second.nonce)
                {
                    JLOG(j_.warn())
                        << "SNTP: Reply from " << ep_ << "had wrong nonce";
                }
                else
                {
                    processReply();
                }
            }
        }

        socket_.async_receive_from(
            buffer(buf_, 256),
            ep_,
            std::bind(
                &SNTPClientImp::onRead,
                this,
                std::placeholders::_1,
                std::placeholders::_2));
    }

    //--------------------------------------------------------------------------

    void
    addServer(std::string const& server)
    {
        std::lock_guard lock(mutex_);
        servers_.push_back(std::make_pair(server, sys_seconds::max()));
    }

    void
    queryAll()
    {
        while (doQuery())
        {
        }
    }

    bool
    doQuery()
    {
        std::lock_guard lock(mutex_);
        auto best = servers_.end();

        for (auto iter = servers_.begin(), end = best; iter != end; ++iter)
            if ((best == end) || (iter->second == sys_seconds::max()) ||
                (iter->second < best->second))
                best = iter;

        if (best == servers_.end())
        {
            JLOG(j_.trace()) << "SNTP: No server to query";
            return false;
        }

        using namespace std::chrono;
        auto now = time_point_cast<seconds>(clock_type::now());

        if ((best->second != sys_seconds::max()) &&
            ((best->second + NTP_MIN_QUERY) >= now))
        {
            JLOG(j_.trace()) << "SNTP: All servers recently queried";
            return false;
        }

        best->second = now;

        boost::asio::ip::udp::resolver::query query(
            boost::asio::ip::udp::v4(), best->first, "ntp");
        resolver_.async_resolve(
            query,
            std::bind(
                &SNTPClientImp::resolveComplete,
                this,
                std::placeholders::_1,
                std::placeholders::_2));
        JLOG(j_.trace()) << "SNTPClock: Resolve pending for " << best->first;
        return true;
    }

    void
    resolveComplete(
        error_code const& ec,
        boost::asio::ip::udp::resolver::iterator it)
    {
        using namespace boost::asio;
        if (ec == error::operation_aborted)
            return;
        if (ec)
        {
            JLOG(j_.trace()) << "SNTPClock::resolveComplete: " << ec.message();
            return;
        }

        assert(it != ip::udp::resolver::iterator());

        auto sel = it;
        int i = 1;

        while (++it != ip::udp::resolver::iterator())
        {
            if (rand_int(i++) == 0)
                sel = it;
        }

        if (sel != ip::udp::resolver::iterator())
        {
            std::lock_guard lock(mutex_);
            Query& query = queries_[*sel];
            using namespace std::chrono;
            auto now = time_point_cast<seconds>(clock_type::now());

            if ((query.sent == now) || ((query.sent + 1s) == now))
            {
                // This can happen if the same IP address is reached through
                // multiple names
                JLOG(j_.trace()) << "SNTP: Redundant query suppressed";
                return;
            }

            query.replied = false;
            query.sent = now;
            query.nonce = rand_int<std::uint32_t>();
            // The following line of code will overflow at 2036-02-07 06:28:16
            // UTC
            //   due to the 32 bit cast.
            reinterpret_cast<std::uint32_t*>(
                SNTPQueryData)[NTP_OFF_XMITTS_INT] =
                static_cast<std::uint32_t>(
                    (time_point_cast<seconds>(clock_type::now()) +
                     NTP_UNIX_OFFSET)
                        .time_since_epoch()
                        .count());
            reinterpret_cast<std::uint32_t*>(
                SNTPQueryData)[NTP_OFF_XMITTS_FRAC] = query.nonce;
            socket_.async_send_to(
                buffer(SNTPQueryData, 48),
                *sel,
                std::bind(
                    &SNTPClientImp::onSend,
                    this,
                    std::placeholders::_1,
                    std::placeholders::_2));
        }
    }

    void
    onSend(error_code const& ec, std::size_t)
    {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (ec)
        {
            JLOG(j_.warn()) << "SNTPClock::onSend: " << ec.message();
            return;
        }
    }

    void
    processReply()
    {
        using namespace std::chrono;
        assert(buf_.size() >= 48);
        std::uint32_t* recvBuffer =
            reinterpret_cast<std::uint32_t*>(&buf_.front());

        unsigned info = ntohl(recvBuffer[NTP_OFF_INFO]);
        auto timev = seconds{ntohl(recvBuffer[NTP_OFF_RECVTS_INT])};
        unsigned stratum = (info >> 16) & 0xff;

        if ((info >> 30) == 3)
        {
            JLOG(j_.info()) << "SNTP: Alarm condition " << ep_;
            return;
        }

        if ((stratum == 0) || (stratum > 14))
        {
            JLOG(j_.info()) << "SNTP: Unreasonable stratum (" << stratum
                            << ") from " << ep_;
            return;
        }

        using namespace std::chrono;
        auto now = time_point_cast<seconds>(clock_type::now());
        timev -= now.time_since_epoch();
        timev -= NTP_UNIX_OFFSET;

        // add offset to list, replacing oldest one if appropriate
        offsets_.push_back(timev);

        if (offsets_.size() >= NTP_SAMPLE_WINDOW)
            offsets_.pop_front();

        lastUpdate_ = now;

        // select median time
        auto offsetList = offsets_;
        std::sort(offsetList.begin(), offsetList.end());
        auto j = offsetList.size();
        auto it = std::next(offsetList.begin(), j / 2);
        offset_ = *it;

        if ((j % 2) == 0)
            offset_ = (offset_ + (*--it)) / 2;

        // debounce: small corrections likely
        //           do more harm than good
        if ((offset_ == -1s) || (offset_ == 1s))
            offset_ = 0s;

        if (timev != 0s || offset_ != 0s)
        {
            JLOG(j_.trace()) << "SNTP: Offset is " << timev.count()
                             << ", new system offset is " << offset_.count();
        }
    }
};

//------------------------------------------------------------------------------

std::unique_ptr<SNTPClock>
make_SNTPClock(beast::Journal j)
{
    return std::make_unique<SNTPClientImp>(j);
}

}  // namespace ripple
