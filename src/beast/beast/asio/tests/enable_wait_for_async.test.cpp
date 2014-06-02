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

#if BEAST_INCLUDE_BEASTCONFIG
#include <BeastConfig.h>
#endif

#include <beast/unit_test/suite.h>

#include <beast/asio/bind_handler.h>
#include <beast/asio/enable_wait_for_async.h>

#include <boost/asio/io_service.hpp>

namespace beast {

class enable_wait_for_async_test : public unit_test::suite
{
public:
    typedef boost::system::error_code error_code;

    void test()
    {
        struct handler
        {
            void operator()(error_code)
            {
            }
        };

        struct owner : asio::enable_wait_for_async <owner>
        {
            bool notified;

            owner()
                : notified (false)
            {
            }

            void operator()()
            {
                {
                    boost::asio::io_service ios;
                    ios.post (asio::bind_handler (handler(),
                        error_code()));
                    ios.run();
                    ios.reset();
                    wait_for_async();
                }

                {
                    boost::asio::io_service ios;
                    ios.post (wrap_with_counter (asio::bind_handler (
                        handler(), error_code())));
                    ios.run();
                    wait_for_async();
                }

                {
                    boost::asio::io_service ios;
                    handler h;
                    ios.post (wrap_with_counter (std::bind (
                        &handler::operator(), &h,
                            error_code())));
                    ios.run();
                    wait_for_async();
                }
            }

            void on_wait_for_async()
            {
                notified = true;
            }
        };

        owner o;
        o();
        expect (o.notified);
    }

    void run()
    {
        test();
    }
};

BEAST_DEFINE_TESTSUITE(enable_wait_for_async,asio,beast);

}
