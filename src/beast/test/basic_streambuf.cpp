//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/basic_streambuf.hpp>

#include <beast/streambuf.hpp>
#include <beast/detail/unit_test/suite.hpp>
#include <boost/asio/buffer.hpp>
#include <atomic>
#include <memory>
#include <string>

namespace beast {
namespace test {

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

class basic_streambuf_test : public beast::detail::unit_test::suite
{
public:
    template<class ConstBufferSequence>
    static
    std::string
    to_string(ConstBufferSequence const& bs)
    {
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        std::string s;
        s.reserve(buffer_size(bs));
        for(auto const& b : bs)
            s.append(buffer_cast<char const*>(b),
                buffer_size(b));
        return s;
    }

    void
    testPrepare()
    {
        using boost::asio::buffer_size;
        streambuf sb(2);
        expect(buffer_size(sb.prepare(5)) == 5);
        expect(buffer_size(sb.prepare(8)) == 8);
        expect(buffer_size(sb.prepare(7)) == 7);
    }

    void testStreambuf()
    {
        using boost::asio::buffer;
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        std::string const s = "Hello, world";
        expect(s.size() == 12);
        for(std::size_t i = 1; i < 12; ++i) {
        for(std::size_t x = 1; x < 4; ++x) {
        for(std::size_t y = 1; y < 4; ++y) {
        for(std::size_t t = 1; t < 4; ++ t) {
        for(std::size_t u = 1; u < 4; ++ u) {
        std::size_t z = s.size() - (x + y);
        std::size_t v = s.size() - (t + u);
        {
            streambuf sb(i);
            decltype(sb)::mutable_buffers_type d;
            d = sb.prepare(z); expect(buffer_size(d) == z);
            d = sb.prepare(0); expect(buffer_size(d) == 0);
            d = sb.prepare(y); expect(buffer_size(d) == y);
            d = sb.prepare(x); expect(buffer_size(d) == x);
            sb.commit(buffer_copy(d, buffer(s.data(), x)));
            expect(sb.size() == x);
            expect(buffer_size(sb.data()) == sb.size());
            d = sb.prepare(x); expect(buffer_size(d) == x);
            d = sb.prepare(0); expect(buffer_size(d) == 0);
            d = sb.prepare(z); expect(buffer_size(d) == z);
            d = sb.prepare(y); expect(buffer_size(d) == y);
            sb.commit(buffer_copy(d, buffer(s.data()+x, y)));
            sb.commit(1);
            expect(sb.size() == x + y);
            expect(buffer_size(sb.data()) == sb.size());
            d = sb.prepare(x); expect(buffer_size(d) == x);
            d = sb.prepare(y); expect(buffer_size(d) == y);
            d = sb.prepare(0); expect(buffer_size(d) == 0);
            d = sb.prepare(z); expect(buffer_size(d) == z);
            sb.commit(buffer_copy(d, buffer(s.data()+x+y, z)));
            sb.commit(2);
            expect(sb.size() == x + y + z);
            expect(buffer_size(sb.data()) == sb.size());
            expect(to_string(sb.data()) == s);
            sb.consume(t);
            d = sb.prepare(0); expect(buffer_size(d) == 0);
            expect(to_string(sb.data()) == s.substr(t, std::string::npos));
            sb.consume(u);
            expect(to_string(sb.data()) == s.substr(t + u, std::string::npos));
            sb.consume(v);
            expect(to_string(sb.data()) == "");
            sb.consume(1);
            d = sb.prepare(0); expect(buffer_size(d) == 0);
        }
        }}}}}
    }

    template<class Alloc1, class Alloc2>
    static
    bool
    eq(basic_streambuf<Alloc1> const& sb1,
        basic_streambuf<Alloc2> const& sb2)
    {
        return to_string(sb1.data()) == to_string(sb2.data());
    }

    void testSpecial()
    {
        using boost::asio::buffer;
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        std::string const s = "Hello, world";
        expect(s.size() == 12);
        for(std::size_t i = 1; i < 12; ++i) {
        for(std::size_t x = 1; x < 4; ++x) {
        for(std::size_t y = 1; y < 4; ++y) {
        std::size_t z = s.size() - (x + y);
        {
            streambuf sb(i);
            sb.commit(buffer_copy(sb.prepare(x), buffer(s.data(), x)));
            sb.commit(buffer_copy(sb.prepare(y), buffer(s.data()+x, y)));
            sb.commit(buffer_copy(sb.prepare(z), buffer(s.data()+x+y, z)));
            expect(to_string(sb.data()) == s);
            {
                streambuf sb2(sb);
                expect(eq(sb, sb2));
            }
            {
                streambuf sb2;
                sb2 = sb;
                expect(eq(sb, sb2));
            }
            {
                streambuf sb2(std::move(sb));
                expect(to_string(sb2.data()) == s);
                expect(buffer_size(sb.data()) == 0);
                sb = std::move(sb2);
                expect(to_string(sb.data()) == s);
                expect(buffer_size(sb2.data()) == 0);
            }
        }
        }}}
    }

    void testAllocator()
    {
        {
            using alloc_type =
                test_allocator<char, false, false, false, false>;
            using sb_type = basic_streambuf<alloc_type>;
            sb_type sb;
            expect(sb.get_allocator().id() == 1);
        }
        {
            using alloc_type =
                test_allocator<char, false, false, false, false>;
            using sb_type = basic_streambuf<alloc_type>;
            sb_type sb;
            expect(sb.get_allocator().id() == 2);
            sb_type sb2(sb);
            expect(sb2.get_allocator().id() == 2);
            sb_type sb3(sb, alloc_type{});
            //expect(sb3.get_allocator().id() == 3);
        }
    }

    void run() override
    {
        testPrepare();
        testStreambuf();
        testSpecial();
        testAllocator();
    }
};

BEAST_DEFINE_TESTSUITE(basic_streambuf,core,beast);

} // test
} // beast
