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

#include <ripple/beast/asio/ssl_error.h>
#include <ripple/beast/unit_test.h>
#include <string>

namespace beast {

class error_test : public unit_test::suite
{
public:
    void run() override
    {
        {
            boost::system::error_code ec =
                boost::system::error_code (335544539,
                    boost::asio::error::get_ssl_category ());
            std::string s = beast::error_message_with_ssl(ec);
            // strip away last part of the error message since
            // it can vary with openssl versions and/or compile
            // flags
            auto const lastColon = s.find_last_of(':');
            if (lastColon != s.npos)
                s = s.substr(0, lastColon);
            BEAST_EXPECT(s == " (20,0,219) error:140000DB:SSL routines:SSL routines");
        }
    }
};

BEAST_DEFINE_TESTSUITE(error,asio,beast);

} // beast
