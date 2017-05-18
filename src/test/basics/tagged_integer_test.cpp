//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2014, Nikolaos D. Bougalis <nikb@bougalis.net>


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

#include <BeastConfig.h>
#include <ripple/basics/tagged_integer.h>
#include <ripple/beast/unit_test.h>
#include <type_traits>

namespace ripple {
namespace test {

class tagged_integer_test
    : public beast::unit_test::suite
{
private:
    struct Tag1 { };
    struct Tag2 { };

    using TagInt1 = tagged_integer <std::uint32_t, Tag1>;
    using TagInt2 = tagged_integer <std::uint32_t, Tag2>;
    using TagInt3 = tagged_integer <std::uint64_t, Tag1>;

    // Check construction of tagged_integers
    static_assert (std::is_constructible<TagInt1, std::uint32_t>::value,
        "TagInt1 should be constructible using a std::uint32_t");

    static_assert (!std::is_constructible<TagInt1, std::uint64_t>::value,
        "TagInt1 should not be constructible using a std::uint64_t");

    static_assert (std::is_constructible<TagInt3, std::uint32_t>::value,
        "TagInt3 should be constructible using a std::uint32_t");

    static_assert (std::is_constructible<TagInt3, std::uint64_t>::value,
        "TagInt3 should be constructible using a std::uint64_t");

    // Check assignment of tagged_integers
    static_assert (!std::is_assignable<TagInt1, std::uint32_t>::value,
        "TagInt1 should not be assignable with a std::uint32_t");

    static_assert (!std::is_assignable<TagInt1, std::uint64_t>::value,
        "TagInt1 should not be assignable with a std::uint64_t");

    static_assert (!std::is_assignable<TagInt3, std::uint32_t>::value,
        "TagInt3 should not be assignable with a std::uint32_t");

    static_assert (!std::is_assignable<TagInt3, std::uint64_t>::value,
        "TagInt3 should not be assignable with a std::uint64_t");

    static_assert (std::is_assignable<TagInt1, TagInt1>::value,
        "TagInt1 should be assignable with a TagInt1");

    static_assert (!std::is_assignable<TagInt1, TagInt2>::value,
        "TagInt1 should not be assignable with a TagInt2");

    static_assert (std::is_assignable<TagInt3, TagInt3>::value,
        "TagInt3 should be assignable with a TagInt1");

    static_assert (!std::is_assignable<TagInt1, TagInt3>::value,
        "TagInt1 should not be assignable with a TagInt3");

    static_assert (!std::is_assignable<TagInt3, TagInt1>::value,
        "TagInt3 should not be assignable with a TagInt1");

    // Check convertibility of tagged_integers
    static_assert (!std::is_convertible<std::uint32_t, TagInt1>::value,
        "std::uint32_t should not be convertible to a TagInt1");

    static_assert (!std::is_convertible<std::uint32_t, TagInt3>::value,
        "std::uint32_t should not be convertible to a TagInt3");

    static_assert (!std::is_convertible<std::uint64_t, TagInt3>::value,
        "std::uint64_t should not be convertible to a TagInt3");

    static_assert (!std::is_convertible<std::uint64_t, TagInt2>::value,
        "std::uint64_t should not be convertible to a TagInt2");

    static_assert (!std::is_convertible<TagInt1, TagInt2>::value,
        "TagInt1 should not be convertible to TagInt2");

    static_assert (!std::is_convertible<TagInt1, TagInt3>::value,
        "TagInt1 should not be convertible to TagInt3");

    static_assert (!std::is_convertible<TagInt2, TagInt3>::value,
        "TagInt2 should not be convertible to a TagInt3");

public:
    void run ()
    {
        TagInt1 const zero (0);
        TagInt1 const one (1);

        testcase ("Comparison Operators");

        expect (zero >= zero, "Should be greater than or equal");
        expect (zero == zero, "Should be equal");

        expect (one > zero, "Should be greater");
        expect (one >= zero, "Should be greater than or equal");
        expect (one != zero, "Should not be equal");

        unexpected (one < zero, "Should be greater");
        unexpected (one <= zero, "Should not be greater than or equal");
        unexpected (one == zero, "Should not be equal");

        testcase ("Arithmetic Operators");

        TagInt1 tmp;

        tmp = zero + 0u;
        expect (tmp == zero, "Should be equal");

        tmp = 1u + zero;
        expect (tmp == one, "Should be equal");

        expect(--tmp == zero, "Should be equal");
        expect(tmp++ == zero, "Should be equal");
        expect(tmp == one, "Should be equal");

        expect(tmp-- == one, "Should be equal");
        expect(tmp == zero, "Should be equal");
        expect(++tmp == one, "Should be equal");

        tmp = zero;

        tmp += 1u;
        expect(tmp == one, "Should be equal");

        tmp -= 1u;
        expect(tmp == zero, "Should be equal");
    }
};

BEAST_DEFINE_TESTSUITE(tagged_integer,ripple_basics,ripple);

} // test
} // ripple
