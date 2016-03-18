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

#include <beast/asio/append_buffers.h>
#include <beast/asio/clip_buffers.h>
#include <beast/asio/streambuf.h>
#include <beast/asio/static_streambuf.h>
#include <beast/unit_test/suite.h>
#include <boost/asio/buffer.hpp>
#include <array>
#include <list>
#include <string>

namespace beast {
namespace asio {

class buffers_test : public unit_test::suite
{
public:
    void testAppend()
    {
        using namespace boost::asio;
        char buf[10];
        std::list<const_buffer> b1;
        std::vector<const_buffer> b2{
            const_buffer{buf+0, 1},
            const_buffer{buf+1, 2}};
        std::list<const_buffer> b3;
        std::array<const_buffer, 3> b4{
            const_buffer{buf+3, 1},
            const_buffer{buf+4, 2},
            const_buffer{buf+6, 3}};
        std::list<const_buffer> b5{
            const_buffer{buf+9, 1}};
        std::list<const_buffer> b6;
        auto bs = append_buffers(
            b1, b2, b3, b4, b5, b6);
        expect(buffer_size(bs) == 10);
        std::vector<const_buffer> v;
        for(auto iter = std::make_reverse_iterator(bs.end());
                iter != std::make_reverse_iterator(bs.begin()); ++iter)
            v.emplace_back(*iter);
        expect(buffer_size(bs) == 10);
        decltype(bs) bs2(bs);
        auto bs3(std::move(bs));
        bs = bs2;
        bs3 = std::move(bs2);
        {
            streambuf sb1, sb2;
            expect(buffer_size(append_buffers(
                sb1.prepare(5), sb2.prepare(7))) == 12);
            sb1.commit(5);
            sb2.commit(7);
            expect(buffer_size(append_buffers(
                sb1.data(), sb2.data())) == 12);
        }
    }

    void testClip()
    {
        using namespace boost::asio;
        std::vector<const_buffer> bs;
        expect(buffer_size(clip_buffers(1, bs)) == 0);
        bs.emplace_back(nullptr, 10);
        expect(buffer_size(clip_buffers(0, bs)) == 0);
        expect(buffer_size(clip_buffers(6, bs)) == 6);
        expect(buffer_size(clip_buffers(10, bs)) == 10);
        bs.emplace_back(nullptr, 20);
        bs.emplace_back(nullptr, 30);
        expect(buffer_size(clip_buffers(15, bs)) == 15);
        expect(buffer_size(clip_buffers(35, bs)) == 35);
        expect(buffer_size(clip_buffers(60, bs)) == 60);
        expect(buffer_size(clip_buffers(70, bs)) == 60);
        {
            streambuf sb;
            expect(buffer_size(clip_buffers(
                3, sb.prepare(5))) == 3);
            sb.commit(3);
            expect(buffer_size(clip_buffers(
                1, sb.data())) == 1);
        }
    }

    void testStatic()
    {
        using namespace boost::asio;
        static_streambuf_n<32> sb;
        std::string s = "Hello";
        sb.commit(buffer_copy(
            sb.prepare(s.size()), buffer(s)));
        sb.commit(buffer_copy(
            sb.prepare(buffer_size(sb.data())),
                sb.data()));
        expect(sb.size() == 10);
        sb.consume(10);
        expect(sb.size() == 0);
    }

    void run() override
    {
        testAppend();
        testClip();
        testStatic();
    }
};

BEAST_DEFINE_TESTSUITE(buffers,asio,beast);

}
}
