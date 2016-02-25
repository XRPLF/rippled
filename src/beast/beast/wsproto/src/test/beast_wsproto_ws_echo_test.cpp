//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#include <beast/wsproto/src/test/async_echo_peer.h>
#include <beast/wsproto/src/test/sync_echo_peer.h>
#include <boost/asio.hpp>
#include <condition_variable>
#include <mutex>

namespace beast {
namespace wsproto {
namespace test {

class ws_echo_test : public unit_test::suite
{
public:
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;

    void
    run() override
    {
        async_echo_peer s1(true, endpoint_type{
            address_type::from_string("127.0.0.1"),
                6000 }, *this);

        sync_echo_peer s2(true, endpoint_type{
            address_type::from_string("127.0.0.1"),
                6001 }, *this);

        boost::asio::io_service ios;
        boost::asio::signal_set signals(
            ios, SIGINT, SIGTERM);
        std::mutex m;
        bool stop = false;
        std::condition_variable cv;
        signals.async_wait(
            [&](boost::system::error_code const& ec,
                int signal_number)
            {
                std::lock_guard<std::mutex> lock(m);
                stop = true;
                cv.notify_one();
            });
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&]{ return stop; });
    }
};

class ws_client_test : public unit_test::suite
{
public:
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;

    void
    run() override
    {
        pass();
        {
            async_echo_peer s1(false, endpoint_type{
                address_type::from_string("127.0.0.1"),
                    9001 }, *this);
        }
        {
            sync_echo_peer s2(false, endpoint_type{
                address_type::from_string("127.0.0.1"),
                    9001 }, *this);
        }
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(ws_echo, asio, beast);
BEAST_DEFINE_TESTSUITE_MANUAL(ws_client, asio, beast);

} // test
} // wsproto
} // beast
