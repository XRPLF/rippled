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
#include <beast/asio/streambuf.h>

namespace beast {
namespace asio {

class streambuf_test : public unit_test::suite
{
public:
    template <class Streambuf>
    void
    prepare (std::size_t n, Streambuf& sb)
    {
        log << "prepare(" << n << "), output = " <<
            boost::asio::buffer_size(sb.prepare(n)) <<
                ", input = " << boost::asio::buffer_size(sb.data()) <<
                    ", size = " << sb.size();
    }

    template <class Streambuf>
    void
    commit (std::size_t n, Streambuf& sb)
    {
        sb.commit(n);
        log << "commit(" << n << "), input = " <<
            boost::asio::buffer_size(sb.data()) <<
                ", size = " << sb.size();
    }

    template <class Streambuf>
    void
    consume (std::size_t n, Streambuf& sb)
    {
        sb.consume(n);
        log << "consume(" << n << "), input = " <<
            boost::asio::buffer_size(sb.data()) <<
                ", size = " << sb.size();
    }

    void run()
    {
        streambuf sb(100);
        sb.prepare(50);
        sb.commit(0);

#if 0
        prepare (100, sb);
        commit (100, sb);
        consume (100, sb);
#endif

#if 0
        prepare (50, sb);
        commit (50, sb);
        prepare (0, sb);
#endif

#if 0
        prepare (200, sb);
        commit (125, sb);
#endif

#if 0
        prepare (10, sb); commit (5, sb);
        prepare (20, sb); commit (15, sb);
#endif

#if 0
        prepare (50, sb);
        commit (25, sb);
        prepare (0, sb);
        consume (12, sb);
        consume (13, sb);
        prepare (1, sb);
#endif

        //log << "output size = " << boost::asio::buffer_size(sb.prepare(35));
        //sb.commit (50); log << "input size = " << boost::asio::buffer_size(sb.data());
        //sb.commit (8); log << "input size = " << boost::asio::buffer_size(sb.data());
        //sb.commit (5); log << "input size = " << boost::asio::buffer_size(sb.data());
        //sb.commit (22); log << "input size = " << boost::asio::buffer_size(sb.data());
        //log << "output size = " << boost::asio::buffer_size(sb.prepare(55));
        //log << "output size = " << boost::asio::buffer_size(sb.prepare(500));
        //log << "output size = " << boost::asio::buffer_size(sb.prepare(1));
        //sb.commit (20); log << "input size = " << boost::asio::buffer_size(sb.data());

        pass();
    }
};


BEAST_DEFINE_TESTSUITE_MANUAL(streambuf,asio,beast);

}
}
