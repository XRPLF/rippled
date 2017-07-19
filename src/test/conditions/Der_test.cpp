//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/beast/unit_test.h>
#include <ripple/conditions/impl/Der.h>
#include <test/conditions/DerChoice.h>

#include <boost/math/special_functions/sign.hpp>

#include <bitset>
#include <type_traits>
#include <vector>

namespace ripple {
namespace test {

class Der_test : public beast::unit_test::suite
{
    void
    writeBuf(std::vector<char> const& b)
    {
        using namespace std;
        ios::fmtflags f(cout.flags());

        cerr << " " << hex;
        cerr << '{';
        for (auto&& e : b)
            cerr << " " << setw(2) << setfill('0') << int(uint8_t(e));
        cerr << '}';

        cout.flags(f);
    }
    template <class T>
    void
    writeDiff(
        T const& v,
        std::vector<char> const& expected,
        std::vector<char> const& encoded)
    {
        auto const maxOutput = 64;
        if (expected.size() > maxOutput || encoded.size() > maxOutput)
        {
            std::vector<char> const shortExp{expected.begin(),
                                             expected.begin() + maxOutput};
            std::vector<char> const shortEnc{encoded.begin(),
                                             encoded.begin() + maxOutput};
            writeDiff(v, shortExp, shortEnc);
            return;
        }
        // std::cerr << v << '\n';
        writeBuf(expected);
        std::cerr << '\n';
        writeBuf(encoded);
        std::cerr << "\n\n";
    };

    template <class T>
    void
    test(
        T const& v,
        std::vector<char> const& expected,
        cryptoconditions::der::TagMode tagMode =
            cryptoconditions::der::TagMode::direct)
    {
        using namespace cryptoconditions::der;
        {
            Encoder s{tagMode};
            s << v << eos;
            std::error_code ec;
            auto const& encoded = s.serializationBuffer(ec);
            if (expected != encoded)
                writeDiff(v, expected, encoded);
            BEAST_EXPECT(!s.ec() && !ec && expected == encoded);
        }

        {
            Decoder s(makeSlice(expected), tagMode);
            T decoded{};
            s >> decoded >> eos;
            BEAST_EXPECT(decoded == v);
            BEAST_EXPECT(!s.ec());
            if (decoded != v || s.ec())
            {
                std::cerr << "Decoded mismatch: " << s.ec().message() << '\n';
            }
        }
    }

    template <class T>
    void
    test(
        T const& v,
        std::string const& expected,
        cryptoconditions::der::TagMode tagMode =
            cryptoconditions::der::TagMode::direct)
    {
        std::vector<char> ev(expected.begin(), expected.end());
        test(v, ev, tagMode);
    }

    static
    int
    vecCmp(std::vector<char> const& lhs, std::vector<char> const& rhs)
    {
        auto const lhsL = lhs.size();
        auto const rhsL = rhs.size();
        auto const commonL = std::min(lhsL, rhsL);
        for (size_t i = 0; i < commonL; ++i)
        {
            auto const lhsV = static_cast<std::uint8_t>(lhs[i]);
            auto const rhsV = static_cast<std::uint8_t>(rhs[i]);

            if (lhsV != rhsV)
            {
                if (lhsV < rhsV)
                    return -1;
                return 1;
            }
        }

        return (lhsL > rhsL) - (lhsL < rhsL);
    };

    void
    testInts()
    {
        testcase("ints");

        using v = std::vector<char>;
        test(0u, v{2, 1, 0});
        test(1u, v{2, 1, 1});
        test(0xffu, v{2, 2, 0, -1});
        test(0xfeu, v{2, 2, 0, -2});
        test(-1, v{2, 1, -1});
        test(-2, v{2, 1, -2});
        test(std::int32_t(0xffffff00), v{2, 2, -1, 0});
        test(std::uint32_t(0xfffffffe), v{2, 5, 0, -1, -1, -1, -2});
        // make sure writes initial zero octet when skipping zeros
        test(std::int32_t(210), v{2, 2, 0, -46});
        test(std::uint64_t(0x101), v{2, 2, 1, 1});
        test(std::uint64_t(0x1000), v{2, 2, 16, 0});
        test(std::uint64_t(0x10001), v{2, 3, 1, 0, 1});
        test(std::uint64_t(0x100000), v{2, 3, 16, 0, 0});
        test(std::uint64_t(0x1001001), v{2, 4, 1, 0, 16, 1});
        test(std::uint64_t(0x1001001), v{2, 4, 1, 0, 16, 1});
        test(
            std::uint64_t(0x1000000000000000),
            v{2, 8, 16, 0, 0, 0, 0, 0, 0, 0});

        {
            // test compare
            std::vector<std::pair<std::int64_t, std::vector<char>>> cases = {
                {std::make_pair(0, v{2, 1, 0}),
                 std::make_pair(1, v{2, 1, 1}),
                 std::make_pair(0xffu, v{2, 2, 0, -1}),
                 std::make_pair(0xfeu, v{2, 2, 0, -2}),
                 std::make_pair(-1, v{2, 1, -1}),
                 std::make_pair(-2, v{2, 1, -2}),
                 std::make_pair(std::int32_t(0xffffff00), v{2, 2, -1, 0}),
                 std::make_pair(
                     std::uint32_t(0xfffffffe), v{2, 5, 0, -1, -1, -1, -2}),
                 std::make_pair(210, v{2, 2, 0, -46}),
                 std::make_pair(0x101, v{2, 2, 1, 1}),
                 std::make_pair(0x1000, v{2, 2, 16, 0}),
                 std::make_pair(0x10001, v{2, 3, 1, 0, 1}),
                 std::make_pair(0x100000, v{2, 3, 16, 0, 0}),
                 std::make_pair(0x1001001, v{2, 4, 1, 0, 16, 1}),
                 std::make_pair(0x1001001, v{2, 4, 1, 0, 16, 1})}};

            cryptoconditions::der::TraitsCache dummy;
            for (auto i = cases.begin(), e = cases.end(); i != e; ++i)
            {
                using Traits =
                    cryptoconditions::der::DerCoderTraits<std::int64_t>;
                this->BEAST_EXPECT(
                    Traits::compare(i->first, i->first, dummy) == 0);
                for (auto j = i + 1; j != e; ++j)
                {
                    this->BEAST_EXPECT(
                        boost::math::sign(
                            Traits::compare(i->first, j->first, dummy)) ==
                        boost::math::sign(vecCmp(i->second, j->second)));
                }
            }
        }
    }

    void
    testString()
    {
        testcase("octet string");
        auto makeTestCase = [](
            size_t n,
            std::vector<char> const& expectedHeader,
            char fillChar) -> std::pair<std::string, std::vector<char>> {
            std::string s;
            s.resize(n);
            std::fill_n(s.begin(), n, fillChar);
            std::vector<char> expected(expectedHeader);
            auto const headerEnd = expected.size();
            if (n)
            {
                expected.resize(headerEnd + n);
                std::fill_n(&expected[headerEnd], n, fillChar);
            }
            return std::make_pair(s, expected);
        };

        std::vector<std::pair<std::string, std::vector<char>>> cases = {{
            makeTestCase(0, {4, 0}, 'a'),
            makeTestCase(0, {4, 0}, 'z'),
            makeTestCase(1, {4, 1}, 'a'),
            makeTestCase(1, {4, 1}, 'z'),
            makeTestCase(127, {4, 127}, 'a'),
            makeTestCase(127, {4, 127}, 'z'),
            makeTestCase(128, {4, -127, -128}, 'a'),
            makeTestCase(128, {4, -127, -128}, 'z'),
            makeTestCase(66000, {4, -125, 1, 1, -48}, 'a'),
            makeTestCase(66000, {4, -125, 1, 1, -48}, 'z'),
        }};

        cryptoconditions::der::TraitsCache dummy;
        for (auto i = cases.begin(), e = cases.end(); i != e; ++i)
        {
            test(i->first, i->second);
            using Traits = cryptoconditions::der::DerCoderTraits<std::string>;
            BEAST_EXPECT(Traits::compare(i->first, i->first, dummy) == 0);
            for (auto j = i + 1; j != e; ++j)
            {
                this->BEAST_EXPECT(
                    boost::math::sign(
                        Traits::compare(i->first, j->first, dummy)) ==
                    boost::math::sign(vecCmp(i->second, j->second)));
            }
        }
    }

    void
    testBitstring()
    {
        testcase("bit string");

        using namespace std::string_literals;

        auto doTest = [this](auto const& col, auto numBitsParam) {
            // visual studio can't handle bitset<numbits>
            constexpr typename decltype(numBitsParam)::value_type numBits =
                decltype(numBitsParam)::value;
            for (auto const& ts : col)
            {
                std::bitset<numBits> bitset{ts.first};
                test(bitset, ts.second);
            }

            using Traits = cryptoconditions::der::DerCoderTraits<std::bitset<numBits>>;
            // check that comare works
            for(auto i = col.begin(), e=col.end(); i!=e; ++i)
            {
                std::bitset<numBits> bitsetI{i->first};
                cryptoconditions::der::TraitsCache dummy;
                this->BEAST_EXPECT(Traits::compare(bitsetI, bitsetI, dummy) == 0);
                for(auto j = i+1; j!=e; ++j)
                {
                    std::bitset<numBits> bitsetJ{j->first};
                    this->BEAST_EXPECT(
                        boost::math::sign(
                            Traits::compare(bitsetI, bitsetJ, dummy)) ==
                        boost::math::sign(i->second.compare(j->second)));
                }
            }
        };

        {
            // Test all combinations of last five bits
            std::array<std::pair<unsigned long long, std::string>, 32>
                testCases = {{std::make_pair(0ull, "\x03\x02\x07\x00"s),
                              std::make_pair(1ull, "\x03\x02\x07\x80"s),
                              std::make_pair(2ull, "\x03\x02\x06\x40"s),
                              std::make_pair(3ull, "\x03\x02\x06\xc0"s),
                              std::make_pair(4ull, "\x03\x02\x05\x20"s),
                              std::make_pair(5ull, "\x03\x02\x05\xa0"s),
                              std::make_pair(6ull, "\x03\x02\x05\x60"s),
                              std::make_pair(7ull, "\x03\x02\x05\xe0"s),
                              std::make_pair(8ull, "\x03\x02\x04\x10"s),
                              std::make_pair(9ull, "\x03\x02\x04\x90"s),
                              std::make_pair(10ull, "\x03\x02\x04\x50"s),
                              std::make_pair(11ull, "\x03\x02\x04\xd0"s),
                              std::make_pair(12ull, "\x03\x02\x04\x30"s),
                              std::make_pair(13ull, "\x03\x02\x04\xb0"s),
                              std::make_pair(14ull, "\x03\x02\x04\x70"s),
                              std::make_pair(15ull, "\x03\x02\x04\xf0"s),
                              std::make_pair(16ull, "\x03\x02\x03\x08"s),
                              std::make_pair(17ull, "\x03\x02\x03\x88"s),
                              std::make_pair(18ull, "\x03\x02\x03\x48"s),
                              std::make_pair(19ull, "\x03\x02\x03\xc8"s),
                              std::make_pair(20ull, "\x03\x02\x03\x28"s),
                              std::make_pair(21ull, "\x03\x02\x03\xa8"s),
                              std::make_pair(22ull, "\x03\x02\x03\x68"s),
                              std::make_pair(23ull, "\x03\x02\x03\xe8"s),
                              std::make_pair(24ull, "\x03\x02\x03\x18"s),
                              std::make_pair(25ull, "\x03\x02\x03\x98"s),
                              std::make_pair(26ull, "\x03\x02\x03\x58"s),
                              std::make_pair(27ull, "\x03\x02\x03\xd8"s),
                              std::make_pair(28ull, "\x03\x02\x03\x38"s),
                              std::make_pair(29ull, "\x03\x02\x03\xb8"s),
                              std::make_pair(30ull, "\x03\x02\x03\x78"s),
                              std::make_pair(31ull, "\x03\x02\x03\xf8"s)}};
            doTest(testCases, std::integral_constant<std::size_t, 5>{});
            doTest(testCases, std::integral_constant<std::size_t, 16>{});
        }

        {
            // test all combinations of five bits that straddle byte boundary
            // between 2nd and 3rd byte: 2 bits in the second byte, 3 bits in
            // the third byte

            std::array<std::pair<unsigned long long, std::string>, 32>
                testCases = {
                    {std::make_pair(0ull, "\x03\x02\x07\x00"s),
                     std::make_pair(16384ull, "\x03\x03\x01\x00\x02"s),
                     std::make_pair(32768ull, "\x03\x03\x00\x00\x01"s),
                     std::make_pair(49152ull, "\x03\x03\x00\x00\x03"s),
                     std::make_pair(65536ull, "\x03\x04\x07\x00\x00\x80"s),
                     std::make_pair(81920ull, "\x03\x04\x07\x00\x02\x80"s),
                     std::make_pair(98304ull, "\x03\x04\x07\x00\x01\x80"s),
                     std::make_pair(114688ull, "\x03\x04\x07\x00\x03\x80"s),
                     std::make_pair(131072ull, "\x03\x04\x06\x00\x00\x40"s),
                     std::make_pair(147456ull, "\x03\x04\x06\x00\x02\x40"s),
                     std::make_pair(163840ull, "\x03\x04\x06\x00\x01\x40"s),
                     std::make_pair(180224ull, "\x03\x04\x06\x00\x03\x40"s),
                     std::make_pair(196608ull, "\x03\x04\x06\x00\x00\xc0"s),
                     std::make_pair(212992ull, "\x03\x04\x06\x00\x02\xc0"s),
                     std::make_pair(229376ull, "\x03\x04\x06\x00\x01\xc0"s),
                     std::make_pair(245760ull, "\x03\x04\x06\x00\x03\xc0"s),
                     std::make_pair(262144ull, "\x03\x04\x05\x00\x00\x20"s),
                     std::make_pair(278528ull, "\x03\x04\x05\x00\x02\x20"s),
                     std::make_pair(294912ull, "\x03\x04\x05\x00\x01\x20"s),
                     std::make_pair(311296ull, "\x03\x04\x05\x00\x03\x20"s),
                     std::make_pair(327680ull, "\x03\x04\x05\x00\x00\xa0"s),
                     std::make_pair(344064ull, "\x03\x04\x05\x00\x02\xa0"s),
                     std::make_pair(360448ull, "\x03\x04\x05\x00\x01\xa0"s),
                     std::make_pair(376832ull, "\x03\x04\x05\x00\x03\xa0"s),
                     std::make_pair(393216ull, "\x03\x04\x05\x00\x00\x60"s),
                     std::make_pair(409600ull, "\x03\x04\x05\x00\x02\x60"s),
                     std::make_pair(425984ull, "\x03\x04\x05\x00\x01\x60"s),
                     std::make_pair(442368ull, "\x03\x04\x05\x00\x03\x60"s),
                     std::make_pair(458752ull, "\x03\x04\x05\x00\x00\xe0"s),
                     std::make_pair(475136ull, "\x03\x04\x05\x00\x02\xe0"s),
                     std::make_pair(491520ull, "\x03\x04\x05\x00\x01\xe0"s),
                     std::make_pair(507904ull, "\x03\x04\x05\x00\x03\xe0"s)}};
            doTest(testCases, std::integral_constant<std::size_t, 24>{});
        }
    }

    void
    testSequence()
    {
        testcase("sequence");

        using namespace cryptoconditions::der;
        {
            cryptoconditions::der::Encoder s{TagMode::direct};
            {
                std::vector<int> v({10});
                s << make_sequence(v) << eos;
            }
            std::vector<char> expected({48, 3, 2, 1, 10});
            std::error_code ec;
            auto const& encoded = s.serializationBuffer(ec);
            BEAST_EXPECT(!s.ec() && !ec && expected == encoded);
        }
        {
            cryptoconditions::der::Encoder s{TagMode::direct};
            {
                std::vector<std::uint64_t> v({10, 100000, std::uint64_t(100000000000)});
                s << make_sequence(v) << eos;
            }
            std::vector<char>
                    expected({48, 15, 2, 1, 10, 2, 3, 1, -122, -96, 2, 5, 23,
                              72, 118, -24, 0});
            std::error_code ec;
            auto const& encoded = s.serializationBuffer(ec);
            BEAST_EXPECT(!s.ec() && !ec && expected == encoded);
        }

        {
            std::vector<std::int64_t> v({10, 100000, 100000000000});
            std::vector<char>
                expected({48, 15, 2, 1, 10, 2, 3, 1, -122, -96, 2, 5, 23, 72, 118,
                          -24, 0});
            std::error_code ec;

            Encoder encoder{TagMode::direct};
            encoder << make_sequence(v) << eos;
            auto const& encoded = encoder.serializationBuffer(ec);
            BEAST_EXPECT(!encoder.ec() && !ec && expected == encoded);

            Decoder decoder(makeSlice(encoded), TagMode::direct);
            v.clear();
            decoder >> make_sequence(v) >> eos;
            BEAST_EXPECT(
                v.size() == 3 && v[0] == 10 && v[1] == 100000 &&
                v[2] == 100000000000);
            BEAST_EXPECT(!decoder.ec());
        }

        {
            std::string stringVal("hello");
            std::uint64_t intVal(42);
            auto tup = std::tie(stringVal, intVal);

            Encoder encoder{TagMode::direct};
            encoder << tup << eos;
            std::error_code ec;
            auto const& encoded = encoder.serializationBuffer(ec);
            BEAST_EXPECT(!encoder.ec() && !ec);

            {
                intVal = 0;
                stringVal.clear();
                Decoder decoder(makeSlice(encoded), TagMode::direct);
                decoder >> tup >> eos;
                BEAST_EXPECT(intVal == 42 && stringVal == std::string("hello"));
                BEAST_EXPECT(!decoder.ec());
            }
            {
                intVal = 0;
                stringVal.clear();
                Decoder decoder(makeSlice(encoded), TagMode::direct);
                decoder >> std::tie(stringVal, intVal) >> eos;
                BEAST_EXPECT(intVal == 42 && stringVal == std::string("hello"));
                BEAST_EXPECT(!decoder.ec());
            }
        }

        {
            auto makeCase = [](
                std::initializer_list<int> val,
                std::initializer_list<char> encoding)
                -> std::pair<std::vector<int>, std::vector<char>> {
                return std::make_pair(
                    std::vector<int>(val), std::vector<char>(encoding));
            };

            std::vector<std::pair<std::vector<int>, std::vector<char>>> cases =
                {{makeCase({100, 1, 10}, {48, 9, 2, 1, 100, 2, 1, 1, 2, 1, 10}),
                  makeCase({100, 11, 1}, {48, 9, 2, 1, 100, 2, 1, 11, 2, 1, 1}),
                  makeCase({100, 10, 1}, {48, 9, 2, 1, 100, 2, 1, 10, 2, 1, 1}),
                  makeCase({1, 10, 100}, {48, 9, 2, 1, 1, 2, 1, 10, 2, 1, 100}),
                  makeCase({10, 100, 1}, {48, 9, 2, 1, 10, 2, 1, 100, 2, 1, 1}),
                  makeCase({1, 11}, {48, 6, 2, 1, 1, 2, 1, 11}),
                  makeCase({1, 10}, {48, 6, 2, 1, 1, 2, 1, 10}),
                  makeCase({10, 1}, {48, 6, 2, 1, 10, 2, 1, 1})}};

            using namespace cryptoconditions::der;
            TraitsCache dummy;
            for (auto i = cases.begin(), e = cases.end(); i != e; ++i)
            {
                using Traits = DerCoderTraits<SequenceOfWrapper<std::vector<int>>>;
                auto const wrappedI = make_sequence(i->first);
                this->BEAST_EXPECT(
                    Traits::compare(wrappedI, wrappedI, dummy) == 0);
                for (auto j = i + 1; j != e; ++j)
                {
                    auto const wrappedJ = make_sequence(j->first);
                    this->BEAST_EXPECT(
                        boost::math::sign(
                            Traits::compare(wrappedI, wrappedJ, dummy)) ==
                        boost::math::sign(vecCmp(i->second, j->second)));
                }
            }
        }
    }

    void
    testSet()
    {
        testcase("set");

        using namespace cryptoconditions::der;
        {
            std::vector<int> v({100, 1, 10});
            std::vector<char> expected({49, 9, 2, 1, 1, 2, 1, 10, 2, 1, 100});

            Encoder encoder{TagMode::direct};
            encoder << make_set(v, encoder) << eos;
            std::error_code ec;
            auto const& encoded = encoder.serializationBuffer(ec);
            BEAST_EXPECT(!encoder.ec() && !ec && expected == encoded);

            Decoder decoder(makeSlice(encoded), TagMode::direct);
            v.clear();
            decoder >> make_set(v, decoder) >> eos;
            BEAST_EXPECT(
                v.size() == 3 && v[0] == 1 && v[1] == 10 && v[2] == 100);
            BEAST_EXPECT(!decoder.ec());
        }

        {
            auto makeCase = [](
                std::initializer_list<int> val,
                std::initializer_list<char> encoding)
                -> std::pair<std::vector<int>, std::vector<char>> {
                return std::make_pair(
                    std::vector<int>(val), std::vector<char>(encoding));
            };

            std::vector<std::pair<std::vector<int>, std::vector<char>>> cases =
                {{makeCase({100, 1, 10}, {49, 9, 2, 1, 1, 2, 1, 10, 2, 1, 100}),
                  makeCase({100, 11, 1}, {49, 9, 2, 1, 1, 2, 1, 11, 2, 1, 100}),
                  makeCase({100, 10, 1}, {49, 9, 2, 1, 1, 2, 1, 10, 2, 1, 100}),
                  makeCase({1, 10, 100}, {49, 9, 2, 1, 1, 2, 1, 10, 2, 1, 100}),
                  makeCase({10, 100, 1}, {49, 9, 2, 1, 1, 2, 1, 10, 2, 1, 100}),
                  makeCase({1, 11}, {49, 6, 2, 1, 1, 2, 1, 11}),
                  makeCase({1, 10}, {49, 6, 2, 1, 1, 2, 1, 10}),
                  makeCase({10, 1}, {49, 6, 2, 1, 1, 2, 1, 10})}};

            using namespace cryptoconditions::der;
            TraitsCache dummy;
            for (auto i = cases.begin(), e = cases.end(); i != e; ++i)
            {
                using Traits = DerCoderTraits<SetOfWrapper<std::vector<int>>>;
                auto const wrappedI = make_set(i->first, dummy);
                this->BEAST_EXPECT(
                    Traits::compare(wrappedI, wrappedI, dummy) == 0);
                for (auto j = i + 1; j != e; ++j)
                {
                    auto const wrappedJ = make_set(j->first, dummy);
                    this->BEAST_EXPECT(
                        boost::math::sign(
                            Traits::compare(wrappedI, wrappedJ, dummy)) ==
                        boost::math::sign(vecCmp(i->second, j->second)));
                }
            }
        }
    }

    void
    testChoice()
    {
        testcase("choice");
        using namespace std::string_literals;
        using namespace cryptoconditions::der;
        {
            /*
            db Db ::=
            d2: {name 'FF'H, unsignedInt 256}
            */

            std::unique_ptr<DerChoiceBaseClass> v =
                std::make_unique<DerChoiceDerived2>("\xFF", 256);
            auto const expected =
                "\xA2\x09\x30\x07\x04\x01\xFF\x02\x02\x01\x00"s;
            test(v, expected);
        }
        {
            // test nested objects
            std::vector<char> buf({'a', 'a'});
            std::string str("AA");
            int signedInt = -3;
            std::uint64_t id = 66000;
            int childIndex = 0;  // even indexes are of type derived1, odd
                                 // indexes are of type derived2
            std::function<std::unique_ptr<DerChoiceBaseClass>(int)>
                createDerived =
                    [&](int level) -> std::unique_ptr<DerChoiceBaseClass> {
                ++childIndex;
                if (childIndex % 2)
                {
                    std::vector<std::unique_ptr<DerChoiceBaseClass>> children;
                    if (level > 1)
                    {
                        for (int i = 0; i < 5; ++i)
                            children.emplace_back(createDerived(level - 1));
                    }
                    ++signedInt;
                    ++buf[0];
                    return std::make_unique<DerChoiceDerived1>(
                        buf, std::move(children), signedInt);
                }
                else
                {
                    if (str[1] == 'Z')
                    {
                        ++str[0];
                        str[1] = 'A';
                    }
                    else
                        ++str[1];
                    ++id;
                    return std::make_unique<DerChoiceDerived2>(str, id);
                }
            };

            auto root = createDerived(/*levels*/ 5);
            Encoder encoder{TagMode::direct};
            encoder << root << eos;
            std::error_code ec;
            auto const& encoded = encoder.serializationBuffer(ec);
            BEAST_EXPECT(!encoder.ec() && !ec);

            Decoder decoder(makeSlice(encoded), TagMode::direct);
            std::unique_ptr<DerChoiceBaseClass> readVal;
            decoder >> readVal >> eos;
            BEAST_EXPECT(!decoder.ec());
            BEAST_EXPECT(equal(readVal, root));
        }
    }

    void
    testIllFormed()
    {
        testcase("ill formed");
        using namespace cryptoconditions::der;

        auto testBad = [&](std::vector<char> const& illFormed) {
            std::vector<std::int64_t> v;
            Decoder decoder(makeSlice(illFormed), TagMode::direct);
            decoder >> make_sequence(v) >> eos;
            BEAST_EXPECT(decoder.ec());
        };

        std::vector<char> wellFormed(
            {48, 15, 2, 1, 10, 2, 3, 1, -122, -96, 2, 5, 23, 72, 118, -24, 0});
        // indexes for the preamble starts and length starts
        std::vector<size_t> indexesToChange({0, 1, 2, 3, 5, 6, 10, 11});

        for (auto i : indexesToChange)
        {
            for (auto delta : {-1, 1})
            {
                auto illFormed(wellFormed);
                illFormed[i] += delta;
                testBad(illFormed);
            }
        }

        {
            auto illFormed(wellFormed);
            illFormed.push_back(1);
            testBad(illFormed);
        }
        {
            auto illFormed(wellFormed);
            illFormed.pop_back();
            testBad(illFormed);
        }
        {
            std::vector<char> const illFormed(
                wellFormed.begin() + 1, wellFormed.end());
            testBad(illFormed);
        }
    }

    void
    testAutoTags()
    {
        testcase("auto tags");
        using namespace cryptoconditions::der;

        std::string sVal{"Hello Auto Tags"};
        std::uint32_t uIntVal{42};

        std::vector<char> expected({48,  20,  -128, 15,   72,  101, 108, 108,
                                    111, 32,  65,   117,  116, 111, 32,  84,
                                    97,  103, 115,  -127, 1,   42});

        Encoder encoder{TagMode::automatic};
        encoder << std::tie(sVal, uIntVal) << eos;
        std::error_code ec;
        auto const& encoded = encoder.serializationBuffer(ec);
        BEAST_EXPECT(!ec && !encoder.ec() && expected == encoded);

        {
            Decoder decoder{makeSlice(encoded), TagMode::automatic};
            std::string readSVal;
            std::uint32_t readUIntVal;
            decoder >> std::tie(readSVal, readUIntVal) >> eos;
            BEAST_EXPECT(!decoder.ec());
            BEAST_EXPECT(readSVal == sVal && readUIntVal == uIntVal);
        }
    }

    void
    testAutoChoice()
    {
        testcase("auto choice");
        using namespace std::string_literals;
        using namespace cryptoconditions::der;
        /*
        --<ASN1.PDU CryptoConditions.Condition, CryptoConditions.Db>--

           CryptoConditions DEFINITIONS AUTOMATIC TAGS ::= BEGIN

        Db ::= CHOICE {
          d1   [1] D1,
          d2   [2] D2,
          d3   [3] D3,
          d4   [4] D4,
          d5   [5] D5
        }

        D1 ::= SEQUENCE {
          buf             OCTET STRING,
          subChoices      SEQUENCE OF Db,
          signedInt       INTEGER
        }

        D2 ::= SEQUENCE {
          name               OCTET STRING,
          unsignedInt        INTEGER
        }

        D3 ::= SEQUENCE {
          subChoices      SET OF Db
        }

        D4 ::= SEQUENCE {
          subChoices      SEQUENCE OF Db
        }

        D5 ::= SEQUENCE {
          subChoice          Db ,
          name               OCTET STRING,
          unsignedInt        INTEGER
        }

        END
        */

        auto make_d2_vec =
            [](std::initializer_list<std::pair<std::string, std::uint64_t>> v) {
                std::vector<std::unique_ptr<DerChoiceBaseClass>> result;
                for (auto const& p : v)
                    result.push_back(
                        std::make_unique<DerChoiceDerived2>(p.first, p.second));
                return result;
            };
        {
            // Notice that unlike the other tests, this one is in direct mode
            /*
            db Db ::=
            d2: {name 'FF'H, unsignedInt 64}
            */
            auto const expected = "\xA2\x08\x30\x06\x04\x01\xFF\x02\x01\x40"s;
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived2>("\xFF", 64);
            test(val, expected, TagMode::direct);
        }
        {
            /*
            db Db ::=
            d2: {name 'FF'H, unsignedInt 64}
            */
            auto const expected = "\xA2\x06\x80\x01\xFF\x81\x01\x40"s;
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived2>("\xFF", 64);
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d2: {name ''H, unsignedInt 64}
            */
            auto const expected = "\xA2\x05\x80\x00\x81\x01\x40"s;
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived2>("", 64);
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d4: {subChoices {d2: {name 'FF'H, unsignedInt 64}}}
            */

            auto const expected =
                "\xA4\x0A\xA0\x08\xA2\x06\x80\x01\xFF\x81\x01\x40"s;
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived4>(
                    make_d2_vec({{"\xFF"s, 64}}));
            test(val, expected, TagMode::automatic);
        }
        {
            // Encode all the sequence child numbers. This should fail.
            /*
            db Db ::=
            d4: {subChoices {d2: {name 'FF'H, unsignedInt 64}}}
            */

            auto const expected =
                "\xa4\x0c\xa0\x0a\xa0\x08\xa2\x06\x80\x01\xff\x81\x01\x40"s;
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived4>(
                    make_d2_vec({{"\xFF"s, 64}}));
            Decoder s(makeSlice(expected), TagMode::automatic);
            std::unique_ptr<DerChoiceBaseClass> decoded;
            s >> decoded >> eos;
            BEAST_EXPECT(decoded != val || s.ec());
        }
        {
            /*
            db Db ::=
            d4: {subChoices {d2: {name ''H, unsignedInt 64}}}
            */

            auto const expected =
                "\xA4\x09\xA0\x07\xA2\x05\x80\x00\x81\x01\x40"s;
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived4>(make_d2_vec({{""s, 64}}));
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d3: {subChoices {d2: {name 'FF'H, unsignedInt 64}}}
            */

            auto const expected =
                "\xA3\x0A\xA0\x08\xA2\x06\x80\x01\xFF\x81\x01\x40"s;

            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived3>(
                    make_d2_vec({{"\xFF"s, 64}}));
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d4: {subChoices {}}
            */
            auto const expected = "\xA4\x02\xA0\x00"s;

            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived4>(make_d2_vec({}));
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d3: {subChoices {}}
            */
            auto const expected = "\xA3\x02\xA0\x00"s;

            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived3>(make_d2_vec({}));
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d4: {subChoices {d2: {name 'FF'H, unsignedInt 64}, d2: {name 'FE'H,
            unsignedInt 63}}}
            */

            auto const expected =
                "\xA4\x12\xA0\x10\xA2\x06\x80\x01\xFF\x81\x01\x40\xA2\x06\x80\x01\xFE\x81\x01\x3F"s;
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived4>(
                    make_d2_vec({{"\xFF", 64}, {"\xFE", 63}}));
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d3: {subChoices {d2: {name 'FF'H, unsignedInt 64}, d2: {name 'FE'H,
            unsignedInt 63}}}
            */

            auto const expected =
                "\xA3\x12\xA0\x10\xA2\x06\x80\x01\xFE\x81\x01\x3F\xA2\x06\x80\x01\xFF\x81\x01\x40"s;
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived3>(
                    make_d2_vec({{"\xFF", 64}, {"\xFE", 63}}));
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d4: {subChoices {d4: {subChoices {d2: {name 'FF'H, unsignedInt 64},
                                              d2: {name 'FE'H, unsignedInt
            63}}},
                             d4: {subChoices {d2: {name 'FD'H, unsignedInt 62},
                                              d2: {name 'FC'H, unsignedInt
            61}}}}}
            */

            auto const expected =
                "\xA4\x2A\xA0\x28\xA4\x12\xA0\x10\xA2\x06\x80\x01\xFF\x81\x01"
                "\x40\xA2\x06\x80\x01\xFE\x81\x01\x3F\xA4\x12\xA0\x10\xA2\x06"
                "\x80\x01\xFD\x81\x01\x3E\xA2\x06\x80\x01\xFC\x81\x01\x3D"s;
            std::vector<std::unique_ptr<DerChoiceBaseClass>> subs;
            subs.push_back(std::make_unique<DerChoiceDerived4>(
                make_d2_vec({{"\xFF", 64}, {"\xFE", 63}})));
            subs.push_back(std::make_unique<DerChoiceDerived4>(
                make_d2_vec({{"\xFD", 62}, {"\xFC", 61}})));
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived4>(std::move(subs));
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d3: {subChoices {d3: {subChoices {d2: {name 'FF'H, unsignedInt 64},
                                              d2: {name 'FE'H, unsignedInt
            63}}},
                             d3: {subChoices {d2: {name 'FD'H, unsignedInt 62},
                                              d2: {name 'FC'H, unsignedInt
            61}}}}}
            */

            auto const expected =
                "\xA3\x2A\xA0\x28\xA3\x12\xA0\x10\xA2\x06\x80\x01\xFC\x81\x01"
                "\x3D\xA2\x06\x80\x01\xFD\x81\x01\x3E\xA3\x12\xA0\x10\xA2\x06"
                "\x80\x01\xFE\x81\x01\x3F\xA2\x06\x80\x01\xFF\x81\x01\x40"s;
            std::vector<std::unique_ptr<DerChoiceBaseClass>> subs;
            subs.push_back(std::make_unique<DerChoiceDerived3>(
                make_d2_vec({{"\xFF", 64}, {"\xFE", 63}})));
            subs.push_back(std::make_unique<DerChoiceDerived3>(
                make_d2_vec({{"\xFD", 62}, {"\xFC", 61}})));
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived3>(std::move(subs));
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d4: {subChoices {d4: {subChoices {}},
                             d4: {subChoices {d2: {name 'FD'H, unsignedInt 62},
                                              d2: {name 'FC'H, unsignedInt
            61}}}}}
            */

            auto const expected =
                "\xA4\x1A\xA0\x18\xA4\x02\xA0\x00\xA4\x12\xA0\x10\xA2\x06\x80"
                "\x01\xFD\x81\x01\x3E\xA2\x06\x80\x01\xFC\x81\x01\x3D"s;
            std::vector<std::unique_ptr<DerChoiceBaseClass>> subs;
            subs.push_back(
                std::make_unique<DerChoiceDerived4>(make_d2_vec({})));
            subs.push_back(std::make_unique<DerChoiceDerived4>(
                make_d2_vec({{"\xFD", 62}, {"\xFC", 61}})));
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived4>(std::move(subs));
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d3: {subChoices {d3: {subChoices {}},
                             d3: {subChoices {d2: {name 'FD'H, unsignedInt 62},
                                              d2: {name 'FC'H, unsignedInt
            61}}}}}
            */

            auto const expected =
                "\xA3\x1A\xA0\x18\xA3\x02\xA0\x00\xA3\x12\xA0\x10\xA2\x06\x80"
                "\x01\xFC\x81\x01\x3D\xA2\x06\x80\x01\xFD\x81\x01\x3E"s;
            std::vector<std::unique_ptr<DerChoiceBaseClass>> subs;
            subs.push_back(
                std::make_unique<DerChoiceDerived3>(make_d2_vec({})));
            subs.push_back(std::make_unique<DerChoiceDerived3>(
                make_d2_vec({{"\xFD", 62}, {"\xFC", 61}})));
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived3>(std::move(subs));
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d4: {subChoices {d4: {subChoices {d2: {name 'FF'H, unsignedInt 64},
                                              d2: {name 'FE'H, unsignedInt
            63}}},
                             d4: {subChoices {}}}}
            */

            auto const expected =
                "\xA4\x1A\xA0\x18\xA4\x12\xA0\x10\xA2\x06\x80\x01\xFF\x81\x01"
                "\x40\xA2\x06\x80\x01\xFE\x81\x01\x3F\xA4\x02\xA0\x00"s;
            std::vector<std::unique_ptr<DerChoiceBaseClass>> subs;
            subs.push_back(std::make_unique<DerChoiceDerived4>(
                make_d2_vec({{"\xFF", 64}, {"\xFE", 63}})));
            subs.push_back(
                std::make_unique<DerChoiceDerived4>(make_d2_vec({})));
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived4>(std::move(subs));
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d3: {subChoices {d3: {subChoices {d2: {name 'FF'H, unsignedInt 64},
                                              d2: {name 'FE'H, unsignedInt
            63}}},
                             d3: {subChoices {}}}}
            */

            auto const expected =
                "\xA3\x1A\xA0\x18\xA3\x02\xA0\x00\xA3\x12\xA0\x10\xA2\x06\x80"
                "\x01\xFE\x81\x01\x3F\xA2\x06\x80\x01\xFF\x81\x01\x40"s;
            std::vector<std::unique_ptr<DerChoiceBaseClass>> subs;
            subs.push_back(std::make_unique<DerChoiceDerived3>(
                make_d2_vec({{"\xFF", 64}, {"\xFE", 63}})));
            subs.push_back(
                std::make_unique<DerChoiceDerived3>(make_d2_vec({})));
            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived3>(std::move(subs));
            test(val, expected, TagMode::automatic);
        }
        {
            /*
            db Db ::=
            d5: {subChoice d2: {name 'FE'H, unsignedInt 63}, name 'FF'H,
            unsignedInt 64}
            */
            auto const expected =
                "\xA5\x10\xA0\x08\xA2\x06\x80\x01\xFE\x81\x01\x3F\x81\x01\xFF\x82\x01\x40"s;

            std::unique_ptr<DerChoiceBaseClass> val =
                std::make_unique<DerChoiceDerived5>(
                    std::make_unique<DerChoiceDerived2>("\xFE", 63),
                    "\xFF",
                    64);
            test(val, expected, TagMode::automatic);
        }
    }

    void
    run()
    {
        testInts();
        testString();
        testBitstring();
        testSequence();
        testSet();
        testChoice();
        testIllFormed();
        testAutoTags();
        testAutoChoice();
    }
};

BEAST_DEFINE_TESTSUITE(Der, conditions, ripple);

}  // test
}  // ripple
