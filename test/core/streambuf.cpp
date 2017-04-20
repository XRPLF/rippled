//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/streambuf.hpp>

#include "buffer_test.hpp"
#include <beast/core/buffer_concepts.hpp>
#include <beast/core/to_string.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>

namespace beast {

static_assert(is_DynamicBuffer<streambuf>::value, "");

struct test_allocator_info
{
    std::size_t ncopy = 0;
    std::size_t nmove = 0;
    std::size_t nselect = 0;
};

template<class T,
    bool Assign, bool Move, bool Swap, bool Select>
class test_allocator;

template<class T,
    bool Assign, bool Move, bool Swap, bool Select>
struct test_allocator_base
{
};

template<class T,
    bool Assign, bool Move, bool Swap>
struct test_allocator_base<T, Assign, Move, Swap, true>
{
    static
    test_allocator<T, Assign, Move, Swap, true>
    select_on_container_copy_construction(
        test_allocator<T, Assign, Move, Swap, true> const& a)
    {
        return test_allocator<T, Assign, Move, Swap, true>{};
    }
};

template<class T,
    bool Assign, bool Move, bool Swap, bool Select>
class test_allocator : public test_allocator_base<
        T, Assign, Move, Swap, Select>
{
    std::size_t id_;
    std::shared_ptr<test_allocator_info> info_;

    template<class, bool, bool, bool, bool>
    friend class test_allocator;

public:
    using value_type = T;
    using propagate_on_container_copy_assignment =
        std::integral_constant<bool, Assign>;
    using propagate_on_container_move_assignment =
        std::integral_constant<bool, Move>;
    using propagate_on_container_swap =
        std::integral_constant<bool, Swap>;

    template<class U>
    struct rebind
    {
        using other = test_allocator<
            U, Assign, Move, Swap, Select>;
    };

    test_allocator()
        : id_([]
            {
                static std::atomic<
                    std::size_t> sid(0);
                return ++sid;
            }())
        , info_(std::make_shared<test_allocator_info>())
    {
    }

    test_allocator(test_allocator const& u) noexcept
        : id_(u.id_)
        , info_(u.info_)
    {
        ++info_->ncopy;
    }

    template<class U>
    test_allocator(test_allocator<
            U, Assign, Move, Swap, Select> const& u) noexcept
        : id_(u.id_)
        , info_(u.info_)
    {
        ++info_->ncopy;
    }

    test_allocator(test_allocator&& t)
        : id_(t.id_)
        , info_(t.info_)
    {
        ++info_->nmove;
    }

    value_type*
    allocate(std::size_t n)
    {
        return static_cast<value_type*>(
            ::operator new (n*sizeof(value_type)));
    }

    void
    deallocate(value_type* p, std::size_t) noexcept
    {
        ::operator delete(p);
    }

    std::size_t
    id() const
    {
        return id_;
    }

    test_allocator_info const*
    operator->() const
    {
        return info_.get();
    }
};

class basic_streambuf_test : public beast::unit_test::suite
{
public:
    template<class Alloc1, class Alloc2>
    static
    bool
    eq(basic_streambuf<Alloc1> const& sb1,
        basic_streambuf<Alloc2> const& sb2)
    {
        return to_string(sb1.data()) == to_string(sb2.data());
    }

    template<class ConstBufferSequence>
    void
    expect_size(std::size_t n, ConstBufferSequence const& buffers)
    {
        BEAST_EXPECT(test::size_pre(buffers) == n);
        BEAST_EXPECT(test::size_post(buffers) == n);
        BEAST_EXPECT(test::size_rev_pre(buffers) == n);
        BEAST_EXPECT(test::size_rev_post(buffers) == n);
    }

    template<class U, class V>
    static
    void
    self_assign(U& u, V&& v)
    {
        u = std::forward<V>(v);
    }

    void testSpecialMembers()
    {
        using boost::asio::buffer;
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        std::string const s = "Hello, world";
        BEAST_EXPECT(s.size() == 12);
        for(std::size_t i = 1; i < 12; ++i) {
        for(std::size_t x = 1; x < 4; ++x) {
        for(std::size_t y = 1; y < 4; ++y) {
        std::size_t z = s.size() - (x + y);
        {
            streambuf sb(i);
            sb.commit(buffer_copy(sb.prepare(x), buffer(s.data(), x)));
            sb.commit(buffer_copy(sb.prepare(y), buffer(s.data()+x, y)));
            sb.commit(buffer_copy(sb.prepare(z), buffer(s.data()+x+y, z)));
            BEAST_EXPECT(to_string(sb.data()) == s);
            {
                streambuf sb2(sb);
                BEAST_EXPECT(eq(sb, sb2));
            }
            {
                streambuf sb2;
                sb2 = sb;
                BEAST_EXPECT(eq(sb, sb2));
            }
            {
                streambuf sb2(std::move(sb));
                BEAST_EXPECT(to_string(sb2.data()) == s);
                expect_size(0, sb.data());
                sb = std::move(sb2);
                BEAST_EXPECT(to_string(sb.data()) == s);
                expect_size(0, sb2.data());
            }
            self_assign(sb, sb);
            BEAST_EXPECT(to_string(sb.data()) == s);
            self_assign(sb, std::move(sb));
            BEAST_EXPECT(to_string(sb.data()) == s);
        }
        }}}
        try
        {
            streambuf sb0(0);
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
    }

    void testAllocator()
    {
        // VFALCO This needs work
        {
            using alloc_type =
                test_allocator<char, false, false, false, false>;
            using sb_type = basic_streambuf<alloc_type>;
            sb_type sb;
            BEAST_EXPECT(sb.get_allocator().id() == 1);
        }
        {
            using alloc_type =
                test_allocator<char, false, false, false, false>;
            using sb_type = basic_streambuf<alloc_type>;
            sb_type sb;
            BEAST_EXPECT(sb.get_allocator().id() == 2);
            sb_type sb2(sb);
            BEAST_EXPECT(sb2.get_allocator().id() == 2);
            sb_type sb3(sb, alloc_type{});
        }
    }

    void
    testPrepare()
    {
        using boost::asio::buffer_size;
        {
            streambuf sb(2);
            BEAST_EXPECT(buffer_size(sb.prepare(5)) == 5);
            BEAST_EXPECT(buffer_size(sb.prepare(8)) == 8);
            BEAST_EXPECT(buffer_size(sb.prepare(7)) == 7);
        }
        {
            streambuf sb(2);
            sb.prepare(2);
            BEAST_EXPECT(test::buffer_count(sb.prepare(5)) == 2);
            BEAST_EXPECT(test::buffer_count(sb.prepare(8)) == 3);
            BEAST_EXPECT(test::buffer_count(sb.prepare(4)) == 2);
        }
    }

    void testCommit()
    {
        using boost::asio::buffer_size;
        streambuf sb(2);
        sb.prepare(2);
        sb.prepare(5);
        sb.commit(1);
        expect_size(1, sb.data());
    }

    void testConsume()
    {
        using boost::asio::buffer_size;
        streambuf sb(1);
        expect_size(5, sb.prepare(5));
        sb.commit(3);
        expect_size(3, sb.data());
        sb.consume(1);
        expect_size(2, sb.data());
    }

    void testMatrix()
    {
        using boost::asio::buffer;
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        std::string const s = "Hello, world";
        BEAST_EXPECT(s.size() == 12);
        for(std::size_t i = 1; i < 12; ++i) {
        for(std::size_t x = 1; x < 4; ++x) {
        for(std::size_t y = 1; y < 4; ++y) {
        for(std::size_t t = 1; t < 4; ++ t) {
        for(std::size_t u = 1; u < 4; ++ u) {
        std::size_t z = s.size() - (x + y);
        std::size_t v = s.size() - (t + u);
        {
            streambuf sb(i);
            {
                auto d = sb.prepare(z);
                BEAST_EXPECT(buffer_size(d) == z);
            }
            {
                auto d = sb.prepare(0);
                BEAST_EXPECT(buffer_size(d) == 0);
            }
            {
                auto d = sb.prepare(y);
                BEAST_EXPECT(buffer_size(d) == y);
            }
            {
                auto d = sb.prepare(x);
                BEAST_EXPECT(buffer_size(d) == x);
                sb.commit(buffer_copy(d, buffer(s.data(), x)));
            }
            BEAST_EXPECT(sb.size() == x);
            BEAST_EXPECT(buffer_size(sb.data()) == sb.size());
            {
                auto d = sb.prepare(x);
                BEAST_EXPECT(buffer_size(d) == x);
            }
            {
                auto d = sb.prepare(0);
                BEAST_EXPECT(buffer_size(d) == 0);
            }
            {
                auto d = sb.prepare(z);
                BEAST_EXPECT(buffer_size(d) == z);
            }
            {
                auto d = sb.prepare(y);
                BEAST_EXPECT(buffer_size(d) == y);
                sb.commit(buffer_copy(d, buffer(s.data()+x, y)));
            }
            sb.commit(1);
            BEAST_EXPECT(sb.size() == x + y);
            BEAST_EXPECT(buffer_size(sb.data()) == sb.size());
            {
                auto d = sb.prepare(x);
                BEAST_EXPECT(buffer_size(d) == x);
            }
            {
                auto d = sb.prepare(y);
                BEAST_EXPECT(buffer_size(d) == y);
            }
            {
                auto d = sb.prepare(0);
                BEAST_EXPECT(buffer_size(d) == 0);
            }
            {
                auto d = sb.prepare(z);
                BEAST_EXPECT(buffer_size(d) == z);
                sb.commit(buffer_copy(d, buffer(s.data()+x+y, z)));
            }
            sb.commit(2);
            BEAST_EXPECT(sb.size() == x + y + z);
            BEAST_EXPECT(buffer_size(sb.data()) == sb.size());
            BEAST_EXPECT(to_string(sb.data()) == s);
            sb.consume(t);
            {
                auto d = sb.prepare(0);
                BEAST_EXPECT(buffer_size(d) == 0);
            }
            BEAST_EXPECT(to_string(sb.data()) == s.substr(t, std::string::npos));
            sb.consume(u);
            BEAST_EXPECT(to_string(sb.data()) == s.substr(t + u, std::string::npos));
            sb.consume(v);
            BEAST_EXPECT(to_string(sb.data()) == "");
            sb.consume(1);
            {
                auto d = sb.prepare(0);
                BEAST_EXPECT(buffer_size(d) == 0);
            }
        }
        }}}}}
    }

    void testIterators()
    {
        using boost::asio::buffer_size;
        streambuf sb(1);
        sb.prepare(1);
        sb.commit(1);
        sb.prepare(2);
        sb.commit(2);
        expect_size(3, sb.data());
        sb.prepare(1);
        expect_size(3, sb.prepare(3));
        sb.commit(2);
        BEAST_EXPECT(test::buffer_count(sb.data()) == 4);
    }

    void testOutputStream()
    {
        streambuf sb;
        sb << "x";
        BEAST_EXPECT(to_string(sb.data()) == "x");
    }

    void testCapacity()
    {
        using boost::asio::buffer_size;
        {
            streambuf sb{10};
            BEAST_EXPECT(sb.alloc_size() == 10);
            BEAST_EXPECT(read_size_helper(sb, 1) == 1);
            BEAST_EXPECT(read_size_helper(sb, 10) == 10);
            BEAST_EXPECT(read_size_helper(sb, 20) == 20);
            BEAST_EXPECT(read_size_helper(sb, 1000) == 512);
            sb.prepare(3);
            sb.commit(3);
            BEAST_EXPECT(read_size_helper(sb, 10) == 7);
            BEAST_EXPECT(read_size_helper(sb, 1000) == 7);
        }
        {
            streambuf sb(1000);
            BEAST_EXPECT(sb.alloc_size() == 1000);
            BEAST_EXPECT(read_size_helper(sb, 1) == 1);
            BEAST_EXPECT(read_size_helper(sb, 1000) == 1000);
            BEAST_EXPECT(read_size_helper(sb, 2000) == 1000);
            sb.prepare(3);
            BEAST_EXPECT(read_size_helper(sb, 1) == 1);
            BEAST_EXPECT(read_size_helper(sb, 1000) == 1000);
            BEAST_EXPECT(read_size_helper(sb, 2000) == 1000);
            sb.commit(3);
            BEAST_EXPECT(read_size_helper(sb, 1) == 1);
            BEAST_EXPECT(read_size_helper(sb, 1000) == 997);
            BEAST_EXPECT(read_size_helper(sb, 2000) == 997);
            sb.consume(2);
            BEAST_EXPECT(read_size_helper(sb, 1) == 1);
            BEAST_EXPECT(read_size_helper(sb, 1000) == 997);
            BEAST_EXPECT(read_size_helper(sb, 2000) == 997);
        }
        {
            streambuf sb{2};
            BEAST_EXPECT(sb.alloc_size() == 2);
            BEAST_EXPECT(test::buffer_count(sb.prepare(2)) == 1);
            BEAST_EXPECT(test::buffer_count(sb.prepare(3)) == 2);
            BEAST_EXPECT(buffer_size(sb.prepare(5)) == 5);
            BEAST_EXPECT(read_size_helper(sb, 10) == 6);
        }
        {
            auto avail =
                [](streambuf const& sb)
                {
                    return sb.capacity() - sb.size();
                };
            streambuf sb{100};
            BEAST_EXPECT(sb.alloc_size() == 100);
            BEAST_EXPECT(avail(sb) == 0);
            sb.prepare(100);
            BEAST_EXPECT(avail(sb) == 100);
            sb.commit(100);
            BEAST_EXPECT(avail(sb) == 0);
            sb.consume(100);
            BEAST_EXPECT(avail(sb) == 0);
            sb.alloc_size(200);
            BEAST_EXPECT(sb.alloc_size() == 200);
            sb.prepare(1);
            BEAST_EXPECT(avail(sb) == 200);
        }
    }

    void run() override
    {
        testSpecialMembers();
        testAllocator();
        testPrepare();
        testCommit();
        testConsume();
        testMatrix();
        testIterators();
        testOutputStream();
        testCapacity();
    }
};

BEAST_DEFINE_TESTSUITE(basic_streambuf,core,beast);

} // beast
