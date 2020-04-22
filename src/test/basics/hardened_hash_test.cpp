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

#include <ripple/basics/hardened_hash.h>
#include <ripple/beast/unit_test.h>
#include <boost/functional/hash.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>

namespace ripple {
namespace detail {

template <class T>
class test_user_type_member
{
private:
    T t;

public:
    explicit test_user_type_member(T const& t_ = T()) : t(t_)
    {
    }

    template <class Hasher>
    friend void
    hash_append(Hasher& h, test_user_type_member const& a) noexcept
    {
        using beast::hash_append;
        hash_append(h, a.t);
    }
};

template <class T>
class test_user_type_free
{
private:
    T t;

public:
    explicit test_user_type_free(T const& t_ = T()) : t(t_)
    {
    }

    template <class Hasher>
    friend void
    hash_append(Hasher& h, test_user_type_free const& a) noexcept
    {
        using beast::hash_append;
        hash_append(h, a.t);
    }
};

}  // namespace detail
}  // namespace ripple

//------------------------------------------------------------------------------

namespace ripple {

namespace detail {

template <class T>
using test_hardened_unordered_set = std::unordered_set<T, hardened_hash<>>;

template <class T>
using test_hardened_unordered_map = std::unordered_map<T, int, hardened_hash<>>;

template <class T>
using test_hardened_unordered_multiset =
    std::unordered_multiset<T, hardened_hash<>>;

template <class T>
using test_hardened_unordered_multimap =
    std::unordered_multimap<T, int, hardened_hash<>>;

}  // namespace detail

template <std::size_t Bits, class UInt = std::uint64_t>
class unsigned_integer
{
private:
    static_assert(
        std::is_integral<UInt>::value && std::is_unsigned<UInt>::value,
        "UInt must be an unsigned integral type");

    static_assert(
        Bits % (8 * sizeof(UInt)) == 0,
        "Bits must be a multiple of 8*sizeof(UInt)");

    static_assert(
        Bits >= (8 * sizeof(UInt)),
        "Bits must be at least 8*sizeof(UInt)");

    static std::size_t const size = Bits / (8 * sizeof(UInt));

    std::array<UInt, size> m_vec;

public:
    using value_type = UInt;

    static std::size_t const bits = Bits;
    static std::size_t const bytes = bits / 8;

    template <class Int>
    static unsigned_integer
    from_number(Int v)
    {
        unsigned_integer result;
        for (std::size_t i(1); i < size; ++i)
            result.m_vec[i] = 0;
        result.m_vec[0] = v;
        return result;
    }

    void*
    data() noexcept
    {
        return &m_vec[0];
    }

    void const*
    data() const noexcept
    {
        return &m_vec[0];
    }

    template <class Hasher>
    friend void
    hash_append(Hasher& h, unsigned_integer const& a) noexcept
    {
        using beast::hash_append;
        hash_append(h, a.m_vec);
    }

    friend std::ostream&
    operator<<(std::ostream& s, unsigned_integer const& v)
    {
        for (std::size_t i(0); i < size; ++i)
            s << std::hex << std::setfill('0') << std::setw(2 * sizeof(UInt))
              << v.m_vec[i];
        return s;
    }
};

using sha256_t = unsigned_integer<256, std::size_t>;

#ifndef __INTELLISENSE__
static_assert(sha256_t::bits == 256, "sha256_t must have 256 bits");
#endif

}  // namespace ripple

//------------------------------------------------------------------------------

namespace ripple {

class hardened_hash_test : public beast::unit_test::suite
{
public:
    template <class T>
    void
    check()
    {
        T t{};
        hardened_hash<>()(t);
        pass();
    }

    template <template <class T> class U>
    void
    check_user_type()
    {
        check<U<bool>>();
        check<U<char>>();
        check<U<signed char>>();
        check<U<unsigned char>>();
        // These cause trouble for boost
        // check <U <char16_t>> ();
        // check <U <char32_t>> ();
        check<U<wchar_t>>();
        check<U<short>>();
        check<U<unsigned short>>();
        check<U<int>>();
        check<U<unsigned int>>();
        check<U<long>>();
        check<U<long long>>();
        check<U<unsigned long>>();
        check<U<unsigned long long>>();
        check<U<float>>();
        check<U<double>>();
        check<U<long double>>();
    }

    template <template <class T> class C>
    void
    check_container()
    {
        {
            C<detail::test_user_type_member<std::string>> c;
        }

        pass();

        {
            C<detail::test_user_type_free<std::string>> c;
        }

        pass();
    }

    void
    test_user_types()
    {
        testcase("user types");
        check_user_type<detail::test_user_type_member>();
        check_user_type<detail::test_user_type_free>();
    }

    void
    test_containers()
    {
        testcase("containers");
        check_container<detail::test_hardened_unordered_set>();
        check_container<detail::test_hardened_unordered_map>();
        check_container<detail::test_hardened_unordered_multiset>();
        check_container<detail::test_hardened_unordered_multimap>();
    }

    void
    run() override
    {
        test_user_types();
        test_containers();
    }
};

BEAST_DEFINE_TESTSUITE(hardened_hash, basics, ripple);

}  // namespace ripple
