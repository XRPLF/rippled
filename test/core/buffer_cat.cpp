//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
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
#include <type_traits>
#include <vector>

namespace beast {

class buffer_cat_test : public unit_test::suite
{
public:
    template<class Iterator>
    static
    std::reverse_iterator<Iterator>
    make_reverse_iterator(Iterator i)
    {
        return std::reverse_iterator<Iterator>(i);
    }

    template<class ConstBufferSequence>
    static
    std::size_t
    bsize1(ConstBufferSequence const& bs)
    {
        using boost::asio::buffer_size;
        std::size_t n = 0;
        for(auto it = bs.begin(); it != bs.end(); ++it)
            n += buffer_size(*it);
        return n;
    }

    template<class ConstBufferSequence>
    static
    std::size_t
    bsize2(ConstBufferSequence const& bs)
    {
        using boost::asio::buffer_size;
        std::size_t n = 0;
        for(auto it = bs.begin(); it != bs.end(); it++)
            n += buffer_size(*it);
        return n;
    }

    template<class ConstBufferSequence>
    static
    std::size_t
    bsize3(ConstBufferSequence const& bs)
    {
        using boost::asio::buffer_size;
        std::size_t n = 0;
        for(auto it = bs.end(); it != bs.begin();)
            n += buffer_size(*--it);
        return n;
    }

    template<class ConstBufferSequence>
    static
    std::size_t
    bsize4(ConstBufferSequence const& bs)
    {
        using boost::asio::buffer_size;
        std::size_t n = 0;
        for(auto it = bs.end(); it != bs.begin();)
        {
            it--;
            n += buffer_size(*it);
        }
        return n;
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
        BEAST_EXPECT(buffer_size(bs) == 10);
        BEAST_EXPECT(bsize1(bs) == 10);
        BEAST_EXPECT(bsize2(bs) == 10);
        BEAST_EXPECT(bsize3(bs) == 10);
        BEAST_EXPECT(bsize4(bs) == 10);
        std::vector<const_buffer> v;
        for(auto iter = make_reverse_iterator(bs.end());
                iter != make_reverse_iterator(bs.begin()); ++iter)
            v.emplace_back(*iter);
        BEAST_EXPECT(buffer_size(bs) == 10);
        decltype(bs) bs2(bs);
        auto bs3(std::move(bs));
        {
            boost::asio::streambuf sb1, sb2;
            BEAST_EXPECT(buffer_size(buffer_cat(
                sb1.prepare(5), sb2.prepare(7))) == 12);
            sb1.commit(5);
            sb2.commit(7);
            BEAST_EXPECT(buffer_size(buffer_cat(
                sb1.data(), sb2.data())) == 12);
        }
        for(auto it = bs.begin(); it != bs.end(); ++it)
        {
            decltype(bs)::const_iterator copy;
            copy = it;
            BEAST_EXPECT(copy == it);
            copy = copy;
            BEAST_EXPECT(copy == it);
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
        for(int n = 0;
            n <= std::distance(bs.begin(), bs.end()); ++n)
        {
            auto it = std::next(bs.begin(), n);
            decltype(it) it2(std::move(it));
            it = std::move(it2);
            auto pit = &it;
            it = std::move(*pit);
        }
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

        // decrement iterator
        {
            auto const rbegin =
                make_reverse_iterator(bs.end());
            auto const rend =
                make_reverse_iterator(bs.begin());
            std::size_t n = 0;
            for(auto it = rbegin; it != rend; ++it)
                n += buffer_size(*it);
            BEAST_EXPECT(n == 9);
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
            buffer_size(*bs.end());
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
        auto bs2 = bs;
        BEAST_EXPECT(bs.begin() != bs2.begin());
        BEAST_EXPECT(bs.end() != bs2.end());
        decltype(bs)::const_iterator it;
        decltype(bs2)::const_iterator it2;
        BEAST_EXPECT(it == it2);
    }

    void run() override
    {
        using boost::asio::const_buffer;
        using boost::asio::const_buffers_1;
        using boost::asio::mutable_buffer;
        using boost::asio::mutable_buffers_1;
        struct user_defined : mutable_buffer
        {
        };

        // Check is_all_ConstBufferSequence
        static_assert(
            detail::is_all_ConstBufferSequence<
                const_buffers_1
            >::value, "");
        static_assert(
            detail::is_all_ConstBufferSequence<
                const_buffers_1, const_buffers_1
            >::value, "");
        static_assert(
            detail::is_all_ConstBufferSequence<
                mutable_buffers_1
            >::value, "");
        static_assert(
            detail::is_all_ConstBufferSequence<
                mutable_buffers_1, mutable_buffers_1
            >::value, "");
        static_assert(
            detail::is_all_ConstBufferSequence<
                const_buffers_1, mutable_buffers_1
            >::value, "");
        static_assert(
            ! detail::is_all_ConstBufferSequence<
                const_buffers_1, mutable_buffers_1, int
            >::value, "");

        // Ensure that concatenating mutable buffer
        // sequences results in a mutable buffer sequence
        static_assert(std::is_same<
            mutable_buffer,
            decltype(buffer_cat(
                std::declval<mutable_buffer>(),
                std::declval<user_defined>(),
                std::declval<mutable_buffer>()
                    ))::value_type>::value, "");

        // Ensure that concatenating mixed buffer
        // sequences results in a const buffer sequence.
        static_assert(std::is_same<
            const_buffer,
            decltype(buffer_cat(
                std::declval<mutable_buffer>(),
                std::declval<user_defined>(),
                std::declval<const_buffer>()
                    ))::value_type>::value, "");

        testBufferCat();
        testIterators();
    }
};

BEAST_DEFINE_TESTSUITE(buffer_cat,core,beast);

} // beast
