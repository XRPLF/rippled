//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/buffer_cat.hpp>

#include <beast/unit_test/suite.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/streambuf.hpp>
#include <iterator>
#include <list>
#include <vector>

namespace beast {

class buffer_cat_test : public unit_test::suite
{
public:
    template< class Iterator >
    static
    std::reverse_iterator<Iterator>
    make_reverse_iterator( Iterator i )
    {
        return std::reverse_iterator<Iterator>(i);
    }

    void testBufferCat()
    {
        using boost::asio::buffer_size;
        using boost::asio::const_buffer;
        char buf[10];
        std::list<const_buffer> b1;
        std::vector<const_buffer> b2{
            const_buffer{buf+0, 1},
            const_buffer{buf+1, 2}};
        std::list<const_buffer> b3;
        std::array<const_buffer, 3> b4{{
            const_buffer{buf+3, 1},
            const_buffer{buf+4, 2},
            const_buffer{buf+6, 3}}};
        std::list<const_buffer> b5{
            const_buffer{buf+9, 1}};
        std::list<const_buffer> b6;
        auto bs = buffer_cat(
            b1, b2, b3, b4, b5, b6);
        expect(buffer_size(bs) == 10);
        std::vector<const_buffer> v;
        for(auto iter = make_reverse_iterator(bs.end());
                iter != make_reverse_iterator(bs.begin()); ++iter)
            v.emplace_back(*iter);
        expect(buffer_size(bs) == 10);
        decltype(bs) bs2(bs);
        auto bs3(std::move(bs));
        bs = bs2;
        bs3 = std::move(bs2);
        {
            boost::asio::streambuf sb1, sb2;
            expect(buffer_size(buffer_cat(
                sb1.prepare(5), sb2.prepare(7))) == 12);
            sb1.commit(5);
            sb2.commit(7);
            expect(buffer_size(buffer_cat(
                sb1.data(), sb2.data())) == 12);
        }
    }

    void testIterators()
    {
        using boost::asio::buffer_size;
        using boost::asio::const_buffer;
        char buf[9];
        std::vector<const_buffer> b1{
            const_buffer{buf+0, 1},
            const_buffer{buf+1, 2}};
        std::array<const_buffer, 3> b2{{
            const_buffer{buf+3, 1},
            const_buffer{buf+4, 2},
            const_buffer{buf+6, 3}}};
        auto bs = buffer_cat(b1, b2);

        try
        {
            std::size_t n = 0;
            for(auto it = bs.begin(); n < 100; ++it)
                ++n;
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }

        try
        {
            std::size_t n = 0;
            for(auto it = bs.end(); n < 100; --it)
                ++n;
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }

        try
        {
            expect((buffer_size(*bs.end()) == 0, false));
        }
        catch(std::exception const&)
        {
            pass();
        }
        auto bs2 = bs;
        expect(bs.begin() != bs2.begin());
        expect(bs.end() != bs2.end());
        decltype(bs)::const_iterator it;
        decltype(bs2)::const_iterator it2;
        expect(it == it2);
    }

    void run() override
    {
        testBufferCat();
        testIterators();
    }
};

BEAST_DEFINE_TESTSUITE(buffer_cat,core,beast);

} // beast
