//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/protocol/TER.h>
#include <ripple/beast/unit_test.h>

#include <tuple>
#include <type_traits>

namespace ripple {

struct TER_test : public beast::unit_test::suite
{
    void
    testTransResultInfo()
    {
        for (auto i = -400; i < 400; ++i)
        {
            TER t = TER::fromInt (i);
            auto inRange = isTelLocal(t) ||
                isTemMalformed(t) ||
                isTefFailure(t) ||
                isTerRetry(t) ||
                isTesSuccess(t) ||
                isTecClaim(t);

            std::string token, text;
            auto good = transResultInfo(t, token, text);
            BEAST_EXPECT(inRange || !good);
            BEAST_EXPECT(transToken(t) == (good ? token : "-"));
            BEAST_EXPECT(transHuman(t) == (good ? text : "-"));

            auto code = transCode(token);
            BEAST_EXPECT(good == !!code);
            BEAST_EXPECT(!code || *code == t);
        }
    }

    // Helper template that makes sure two types are not convertible or
    // assignable if not the same.
    // o I1 one tuple index.
    // o I2 other tuple index.
    // o Tup is expected to be a tuple.
    // It's a functor, rather than a function template, since a class template
    // can be a template argument without being full specified.
    template<std::size_t I1, std::size_t I2>
    class NotConvertible
    {
    public:
        template<typename Tup>
        void operator()(Tup const& tup, beast::unit_test::suite&) const
        {
            // Entries in the tuple should not be convertible or assignable
            // unless they are the same types.
            using To_t = std::decay_t<decltype (std::get<I1>(tup))>;
            using From_t = std::decay_t<decltype (std::get<I2>(tup))>;
            static_assert (std::is_same<From_t, To_t>::value ==
                std::is_convertible<From_t, To_t>::value, "Convert err");
            static_assert (std::is_same<To_t, From_t>::value ==
                std::is_constructible<To_t, From_t>::value, "Construct err");
            static_assert (std::is_same <To_t, From_t>::value ==
                std::is_assignable<To_t&, From_t const&>::value, "Assign err");

            // Assignment or conversion from integer to type should never work.
            static_assert (
                ! std::is_convertible<int, To_t>::value, "Convert err");
            static_assert (
                ! std::is_constructible<To_t, int>::value, "Construct err");
            static_assert (
                ! std::is_assignable<To_t&, int const&>::value, "Assign err");
        }
    };

    // Fast iteration over the tuple.
    template<std::size_t I1, std::size_t I2,
        template<std::size_t, std::size_t> class Func, typename Tup>
    std::enable_if_t<I1 != 0>
    testIterate (Tup const& tup, beast::unit_test::suite& suite)
    {
        Func<I1, I2> func;
        func (tup, suite);
        testIterate<I1 - 1, I2, Func>(tup, suite);
    }

    // Slow iteration over the tuple.
    template<std::size_t I1, std::size_t I2,
        template<std::size_t, std::size_t> class Func, typename Tup>
    std::enable_if_t<I1 == 0 && I2 != 0>
    testIterate (Tup const& tup, beast::unit_test::suite& suite)
    {
        Func<I1, I2> func;
        func (tup, suite);
        testIterate<std::tuple_size<Tup>::value - 1, I2 - 1, Func>(tup, suite);
    }

    // Finish iteration over the tuple.
    template<std::size_t I1, std::size_t I2,
        template<std::size_t, std::size_t> class Func, typename Tup>
    std::enable_if_t<I1 == 0 && I2 == 0>
    testIterate (Tup const& tup, beast::unit_test::suite& suite)
    {
        Func<I1, I2> func;
        func (tup, suite);
    }

    void testConversion()
    {
        // Verify that valid conversions are valid and invalid conversions
        // are not valid.

        // Examples of each kind of enum.
        static auto const terEnums = std::make_tuple (telLOCAL_ERROR,
            temMALFORMED, tefFAILURE, terRETRY, tesSUCCESS, tecCLAIM);
        static const int hiIndex {
            std::tuple_size<decltype (terEnums)>::value - 1};

        // Verify that enums cannot be converted to other enum types.
        testIterate<hiIndex, hiIndex, NotConvertible> (terEnums, *this);

        // Lambda that verifies assignability and convertibility.
        auto isConvertable = [] (auto from, auto to)
        {
            using From_t = std::decay_t<decltype (from)>;
            using To_t = std::decay_t<decltype (to)>;
            static_assert (
                std::is_convertible<From_t, To_t>::value, "Convert err");
            static_assert (
                std::is_constructible<To_t, From_t>::value, "Construct err");
            static_assert (
                std::is_assignable<To_t&, From_t const&>::value, "Assign err");
        };

        // Verify the right types convert to NotTEC.
        NotTEC const notTec;
        isConvertable (telLOCAL_ERROR, notTec);
        isConvertable (temMALFORMED,   notTec);
        isConvertable (tefFAILURE,     notTec);
        isConvertable (terRETRY,       notTec);
        isConvertable (tesSUCCESS,     notTec);
        isConvertable (notTec,         notTec);

        // Lambda that verifies types and not assignable or convertible.
        auto notConvertible = [] (auto from, auto to)
        {
            using To_t = std::decay_t<decltype (to)>;
            using From_t = std::decay_t<decltype (from)>;
            static_assert (
                !std::is_convertible<From_t, To_t>::value, "Convert err");
            static_assert (
                !std::is_constructible<To_t, From_t>::value, "Construct err");
            static_assert (
                !std::is_assignable<To_t&, From_t const&>::value, "Assign err");
        };

        // Verify types that shouldn't convert to NotTEC.
        TER const ter;
        notConvertible (tecCLAIM, notTec);
        notConvertible (ter,      notTec);
        notConvertible (4,        notTec);

        // Verify the right types convert to TER.
        isConvertable (telLOCAL_ERROR, ter);
        isConvertable (temMALFORMED,   ter);
        isConvertable (tefFAILURE,     ter);
        isConvertable (terRETRY,       ter);
        isConvertable (tesSUCCESS,     ter);
        isConvertable (tecCLAIM,       ter);
        isConvertable (notTec,         ter);
        isConvertable (ter,            ter);

        // Verify that you can't convert from int to ter.
        notConvertible (4, ter);
    }

    // Helper template that makes sure two types are comparable.  Also
    // verifies that one of the types does not compare to int.
    // o I1 one tuple index.
    // o I2 other tuple index.
    // o Tup is expected to be a tuple.
    // It's a functor, rather than a function template, since a class template
    // can be a template argument without being full specified.
    template<std::size_t I1, std::size_t I2>
    class CheckComparable
    {
    public:
        template<typename Tup>
        void operator()(Tup const& tup, beast::unit_test::suite& suite) const
        {
            // All entries in the tuple should be comparable one to the other.
            auto const lhs = std::get<I1>(tup);
            auto const rhs = std::get<I2>(tup);

            static_assert (std::is_same<
                decltype (operator== (lhs, rhs)), bool>::value, "== err");

            static_assert (std::is_same<
                decltype (operator!= (lhs, rhs)), bool>::value, "!= err");

            static_assert (std::is_same<
                decltype (operator<  (lhs, rhs)), bool>::value, "< err");

            static_assert (std::is_same<
                decltype (operator<= (lhs, rhs)), bool>::value, "<= err");

            static_assert (std::is_same<
                decltype (operator>  (lhs, rhs)), bool>::value, "> err");

            static_assert (std::is_same<
                decltype (operator>= (lhs, rhs)), bool>::value, ">= err");

            // Make sure a sampling of TER types exhibit the expected behavior
            // for all comparison operators.
            suite.expect ((lhs == rhs) == (TERtoInt (lhs) == TERtoInt (rhs)));
            suite.expect ((lhs != rhs) == (TERtoInt (lhs) != TERtoInt (rhs)));
            suite.expect ((lhs <  rhs) == (TERtoInt (lhs) <  TERtoInt (rhs)));
            suite.expect ((lhs <= rhs) == (TERtoInt (lhs) <= TERtoInt (rhs)));
            suite.expect ((lhs >  rhs) == (TERtoInt (lhs) >  TERtoInt (rhs)));
            suite.expect ((lhs >= rhs) == (TERtoInt (lhs) >= TERtoInt (rhs)));
        }
    };

    void testComparison()
    {
        // All of the TER-related types should be comparable.

        // Examples of all the types we expect to successfully compare.
        static auto const ters = std::make_tuple (telLOCAL_ERROR, temMALFORMED,
            tefFAILURE, terRETRY, tesSUCCESS, tecCLAIM,
            NotTEC {telLOCAL_ERROR}, TER {tecCLAIM});
        static const int hiIndex {
            std::tuple_size<decltype (ters)>::value - 1};

        // Verify that all types in the ters tuple can be compared with all
        // the other types in ters.
        testIterate<hiIndex, hiIndex, CheckComparable> (ters, *this);
    }

    void
    run() override
    {
        testTransResultInfo();
        testConversion();
        testComparison();
    }
};

BEAST_DEFINE_TESTSUITE(TER,protocol,ripple);

} //namespace ripple
