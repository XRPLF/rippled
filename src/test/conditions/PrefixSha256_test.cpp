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

#include <ripple/basics/strHex.h>
#include <ripple/beast/unit_test.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/PrefixSha256.h>
#include <ripple/conditions/PreimageSha256.h>
#include <ripple/conditions/Ed25519.h>
#include <ripple/conditions/impl/utils.h>

namespace ripple {
namespace cryptoconditions {

class PrefixSha256_test : public beast::unit_test::suite
{
    void check (
        Fulfillment const& f,
        Condition const& c,
        Slice test,
        Slice good)
    {
        BEAST_EXPECT (validate (f, c, test) ==
            ((test == good) && (f.condition() == c)));
    }

    void testMalformedCondition ()
    {
        testcase ("Malformed Condition");

        // This is malformed and will not load because a
        // feature suite of 0 is not supported.
        auto c1 = loadCondition (
            "cc:1:0:Yja3qFj7NS_VwwE7aJjPJos-uFCzStJlJLD4VsNy2XM:1");
        BEAST_EXPECT (!c1);

        // The following will load but fail in different ways
        auto c2 = loadCondition ( // only sha256
            "cc:1:1:Yja3qFj7NS_VwwE7aJjPJos-uFCzStJlJLD4VsNy2XM:1");
        BEAST_EXPECT (c2 && !validate(*c2));

        auto c3 = loadCondition ( // only preimage
            "cc:1:4:Yja3qFj7NS_VwwE7aJjPJos-uFCzStJlJLD4VsNy2XM:1");
        BEAST_EXPECT (c3 && !validate(*c3));

        auto c4 = loadCondition ( // only sha256+preimage
            "cc:1:5:Yja3qFj7NS_VwwE7aJjPJos-uFCzStJlJLD4VsNy2XM:1");
        BEAST_EXPECT (c4 && !validate(*c4));
    }

    void testPrefix ()
    {
        testcase ("Prefix");

        std::string const prefix1 = "prefix1";
        std::string const prefix2 = "prefix2";

        std::uint8_t msg[8];
        std::iota (std::begin(msg), std::end(msg), std::uint8_t(39));

        std::array<std::uint8_t, 32> const sk =
        {{
            0x50, 0xd8, 0x58, 0xe0, 0x98, 0x5e, 0xcc, 0x7f,
            0x60, 0x41, 0x8a, 0xaf, 0x0c, 0xc5, 0xab, 0x58,
            0x7f, 0x42, 0xc2, 0x57, 0x0a, 0x88, 0x40, 0x95,
            0xa9, 0xe8, 0xcc, 0xac, 0xd0, 0xf6, 0x54, 0x5c
        }};

        {
            PrefixSha256 f1;
            f1.setPrefix(makeSlice (prefix1));
            f1.setSubfulfillment(std::make_unique<PreimageSha256> (
                makeSlice (prefix1)));

            PrefixSha256 f2;
            f2.setPrefix(makeSlice (prefix2));
            f2.setSubfulfillment(std::make_unique<PreimageSha256> (
                makeSlice (prefix1)));

            BEAST_EXPECT (f1 != f2);
            BEAST_EXPECT (f1.condition() != f2.condition());

            // Validating with own condition should succeed.
            BEAST_EXPECT (validate (f1, f1.condition(), {}));
            BEAST_EXPECT (validate (f2, f2.condition(), {}));

            for (std::size_t i = 1; i != sizeof(msg); ++i)
            {
                BEAST_EXPECT (validate (f1, f1.condition(),
                    Slice{msg, i}));
                BEAST_EXPECT (validate (f2, f2.condition(),
                    Slice{msg, i}));
            }

            // The rest should fail:
            BEAST_EXPECT (! validate (f1, f2.condition(), {}));
            BEAST_EXPECT (! validate (f2, f1.condition(), {}));

            for (std::size_t i = 1; i != sizeof(msg); ++i)
            {
                BEAST_EXPECT (! validate (f1, f2.condition(),
                    Slice{msg, i}));
                BEAST_EXPECT (! validate (f2, f1.condition(),
                    Slice{msg, i}));
            }
        }

        {
            PrefixSha256 f1;
            f1.setPrefix(makeSlice (prefix1));
            f1.setSubfulfillment(std::make_unique<PreimageSha256> (
                makeSlice (prefix1)));

            PrefixSha256 f2;
            f2.setPrefix(makeSlice (prefix2));
            f2.setSubfulfillment(std::make_unique<PreimageSha256> (
                makeSlice (prefix2)));

            BEAST_EXPECT (f1 != f2);
            BEAST_EXPECT (f1.condition() != f2.condition());
            BEAST_EXPECT (validate (f1, f1.condition(), {}));
            BEAST_EXPECT (validate (f2, f2.condition(), {}));
            BEAST_EXPECT (! validate (f1, f2.condition(), {}));
            BEAST_EXPECT (! validate (f2, f1.condition(), {}));

            // For preimage conditions, the message shouldn't
            // matter, so verify that it does not:
            for (std::size_t i = 1; i != sizeof(msg); ++i)
            {
                BEAST_EXPECT (validate (f1, f1.condition(),
                    Slice(msg, i)));
                BEAST_EXPECT (validate (f2, f2.condition(),
                    Slice(msg, i)));
                BEAST_EXPECT (! validate (f1, f2.condition(),
                    Slice(msg, i)));
                BEAST_EXPECT (! validate (f2, f1.condition(),
                    Slice(msg, i)));
            }
        }

        {
            PrefixSha256 f1;
            f1.setPrefix(makeSlice (prefix1));
            f1.setSubfulfillment(std::make_unique<Ed25519> (
                SecretKey{ sk }, makeSlice (prefix1)));

            PrefixSha256 f2;
            f2.setPrefix(makeSlice (prefix2));
            f2.setSubfulfillment(std::make_unique<Ed25519> (
                SecretKey{ sk }, makeSlice (prefix2)));

            BEAST_EXPECT (f1 != f2);
            BEAST_EXPECT (f1.condition() != f2.condition());
            BEAST_EXPECT (validate (f1, f1.condition(), {}));
            BEAST_EXPECT (validate (f2, f2.condition(), {}));
            BEAST_EXPECT (! validate (f1, f2.condition(), {}));
            BEAST_EXPECT (! validate (f2, f1.condition(), {}));

            // For non-prefix conditions, the message matters
            // so verify that it does:
            for (std::size_t i = 1; i < sizeof(msg); ++i)
            {
                BEAST_EXPECT (! validate (f1, f1.condition(),
                    Slice(msg, i)));
                BEAST_EXPECT (! validate (f2, f2.condition(),
                    Slice(msg, i)));
                BEAST_EXPECT (! validate (f1, f2.condition(),
                    Slice(msg, i)));
                BEAST_EXPECT (! validate (f2, f1.condition(),
                    Slice(msg, i)));
            }
        }

        { // Test signing with non-empty prefix and non-empty
          // message to ensure that the prefix is properly
          // prepended to the message:
            std::string const m = prefix1 + prefix2;

            // Construct a prefix condition with the prefix
            // prefix1, containing a Ed25519 signature for prefix1+prefix2
            // and check that it passing prefix2 validates, while
            // passing anything else fails:
            PrefixSha256 f;
            f.setPrefix(makeSlice (prefix1));
            f.setSubfulfillment(std::make_unique<Ed25519> (
                SecretKey{ sk }, makeSlice (m)));

            BEAST_EXPECT (to_string(f) ==
                "cf:1:B3ByZWZpeDEABGBEKZMGUASqHkxI9N0BWBlMA"
                "jSbzGZM2W5ADqJpkYqNUTiaLmMYVDHrc-tKqXcmRIT"
                "RFqtYxru4rMSIplCYRP71H9tD09mnfqw4eu5FAJZw1"
                "wa_NOmw78ADIlB4_ENJWAo");

            auto const c = f.condition();

            BEAST_EXPECT (validate (f, c, makeSlice(prefix2)));

            BEAST_EXPECT (! validate (f, c, {}));
            BEAST_EXPECT (! validate (f, c, makeSlice(prefix1)));
            BEAST_EXPECT (! validate (f, c, makeSlice(m)));

            for (std::size_t i = 1; i < sizeof(msg); ++i)
                BEAST_EXPECT (! validate (f, c, Slice(msg, i)));
        }
    }

    void testKnown ()
    {
        testcase ("Known");

        Slice const empty {};
        Slice const abc { "abc", 3 };
        Slice const abcd { "abcd", 4 };
        Slice const vwxyz { "vwxyz", 5 };

        { // empty prefix with an empty PREIMAGE-SHA256 subfulfillment
            auto f = loadFulfillment ("cf:1:AAAAAA");
            BEAST_EXPECT (f);

            {
                auto const f2 = loadFulfillment(makeSlice(to_blob(*f)));
                BEAST_EXPECT (f2);
                BEAST_EXPECT (*f == *f2);
            }

            auto c = loadCondition ("cc:1:7:Yja3qFj7NS_VwwE7aJjPJos-uFCzStJlJLD4VsNy2XM:1");
            BEAST_EXPECT (c);

            {
                auto c1 = loadCondition (to_string(*c));
                BEAST_EXPECT (c1);
                BEAST_EXPECT (*c == *c1);

                auto c2 = loadCondition (makeSlice(to_blob(*c)));
                BEAST_EXPECT (c2);
                BEAST_EXPECT (*c == *c2);
            }

            // Ensure that it has the correct features set
            BEAST_EXPECT (f->features() ==
                (feature_sha256 | feature_prefix | feature_preimage));

            // Test manual construction
            {
                PrefixSha256 f2;
                f2.setPrefix({});
                f2.setSubfulfillment(loadFulfillment ("cf:0:"));

                BEAST_EXPECT (f2 == *f);
                BEAST_EXPECT (f2.condition() == *c);
            }

            // The PREIMAGE-SHA256 we contain validates for
            // any message. So, this condition should work
            // with any buffer:
            check (*f, c.get(), empty, empty);
            check (*f, c.get(), abc, abc);
            check (*f, c.get(), abcd, abcd);
            check (*f, c.get(), vwxyz, vwxyz);
        }

        { // A PREFIX-SHA256 with an empty prefix, wrapping
          // the PREFIX-SHA256 condition we created above
          // which contains a PREIMAGE-SHA256
            auto f = loadFulfillment ("cf:1:AAABBAAAAAA");
            BEAST_EXPECT (f);

            {
                auto const f2 = loadFulfillment(makeSlice(to_blob(*f)));
                BEAST_EXPECT (f2);
                BEAST_EXPECT (*f == *f2);
            }

            auto c = loadCondition ("cc:1:7:Mp5A0CLrJOMAUMe0-qFb-_5U2C0X-iuwwfvumOT0go8:2");
            BEAST_EXPECT (c);

            {
                auto c1 = loadCondition (to_string(*c));
                BEAST_EXPECT (c1);
                BEAST_EXPECT (*c == *c1);

                auto c2 = loadCondition (makeSlice(to_blob(*c)));
                BEAST_EXPECT (c2);
                BEAST_EXPECT (*c == *c2);
            }

            // Ensure that it has the correct features set
            BEAST_EXPECT (f->features() ==
                (feature_sha256 | feature_prefix | feature_preimage));

            // Test manual construction
            {
                PrefixSha256 f2;
                f2.setPrefix({});
                f2.setSubfulfillment(loadFulfillment ("cf:1:AAAAAA"));

                BEAST_EXPECT (f2 == *f);
                BEAST_EXPECT (f2.condition() == *c);
            }

            // The PREIMAGE-SHA256 we contain validates for
            // any message. So, this condition should work
            // with any buffer:
            check (*f, c.get(), empty, empty);
            check (*f, c.get(), abc, abc);
            check (*f, c.get(), abcd, abcd);
            check (*f, c.get(), vwxyz, vwxyz);
        }

        { // A PREFIX-SHA256, with the prefix set to 'abc'
          // that wraps around an ED25519 condition signing
          // the message 'abc':
            auto f = loadFulfillment (
                "cf:1:A2FiYwAEYHahWSBEpuT1ESZbynOmBNkLBSnR32Ar4woZqSV2YNH1rsara"
                "pEir_D33Llmf_YTE2iUcytueMJvW2cxAeJn_i4rZfpNU9rUeKGtpk1Q_R39t9l"
                "JINw-GlZKZHscujVgAQ");
            BEAST_EXPECT (f);

            {
                auto const f2 = loadFulfillment(makeSlice(to_blob(*f)));
                BEAST_EXPECT (f2);
                BEAST_EXPECT (*f == *f2);
            }

            auto c = loadCondition ("cc:1:25:KHqL2K2uisoMhxznwl-6pai-ENDk2x9Wru6Ls63O5Vs:100");
            BEAST_EXPECT (c);

            {
                auto c1 = loadCondition (to_string(*c));
                BEAST_EXPECT (c1);
                BEAST_EXPECT (*c == *c1);

                auto c2 = loadCondition (makeSlice(to_blob(*c)));
                BEAST_EXPECT (c2);
                BEAST_EXPECT (*c == *c2);
            }

            // Ensure that it has the correct features set
            BEAST_EXPECT (f->features() ==
                (feature_sha256 | feature_prefix | feature_ed25519));

            // Test manual construction
            {
                PrefixSha256 f2;
                f2.setPrefix(abc);
                f2.setSubfulfillment(loadFulfillment (
                    "cf:4:dqFZIESm5PURJlvKc6YE2QsFKdHfYCvjChmpJXZg0fWuxqtqkSKv8"
                    "PfcuWZ_9hMTaJRzK254wm9bZzEB4mf-Litl-k1T2tR4oa2mTVD9Hf232Uk"
                    "g3D4aVkpkexy6NWAB"));

                // Check the subfulfillment directly:
                auto sc = f2.subcondition();

                check (f2.subfulfillment(), sc, empty, abc);
                check (f2.subfulfillment(), sc, abc, abc);
                check (f2.subfulfillment(), sc, abcd, abc);
                check (f2.subfulfillment(), sc, vwxyz, abc);

                // This may seem counterintuitive, but it's
                // not: the subfulfillment signed the message
                // "abc"; our prefix is also "abc" so in order
                // to verify this condition successfully, the
                // message must be empty:
                check (f2, c.get(), empty, empty);
                check (f2, c.get(), abc, empty);
                check (f2, c.get(), abcd, empty);
                check (f2, c.get(), vwxyz, empty);
            }

            // Like before, the ED25519 condition we contain
            // signed the message 'abc' which is our prefix
            // which means that this will only validate with
            // an empty message:
            check (*f, c.get(), empty, empty);
            check (*f, c.get(), abc, empty);
            check (*f, c.get(), abcd, empty);
            check (*f, c.get(), vwxyz, empty);
        }
    }

    void testBinaryCodec()
    {
        testcase ("Binary Encoding");

        // A sample prefix+Ed25519 fulfillment and its
        // associated condition:
        std::string const xf =
            "cf:1:DUhlbGxvIFdvcmxkISAABGDsFyuTrV5WO_STL"
            "HDhJFA0w1Rn7y79TWTr-BloNGfiv7YikfrZQy-PKYu"
            "cSkiV2-KT9v_aGmja3wzN719HoMchKl_qPNqXo_TAP"
            "qny6Kwc7IalHUUhJ6vboJ0bbzMcBwo";

        std::string const xc =
            "cc:1:25:1EMtp3YUOBZgeW3lX1lOIoAbUjx9maUty9TMJpMgXo4:110";

        // The subfulfillment for the above, along with its
        // associated condition:
        std::string const xsf =
            "cf:4:7Bcrk61eVjv0kyxw4SRQNMNUZ-8u_U1k6_gZa"
            "DRn4r-2IpH62UMvjymLnEpIldvik_b_2hpo2t8Mze9"
            "fR6DHISpf6jzal6P0wD6p8uisHOyGpR1FISer26CdG"
            "28zHAcK";

        std::string const xsc =
            "cc:4:20:7Bcrk61eVjv0kyxw4SRQNMNUZ-8u_U1k6_gZaDRn4r8:96";

        auto f = loadFulfillment (xf);
        BEAST_EXPECT (f);
        BEAST_EXPECT (to_string(*f) == xf);

        auto c = loadCondition (xc);
        BEAST_EXPECT (c);
        BEAST_EXPECT (to_string(*c) == xc);

        BEAST_EXPECT (f->condition() == c);
        BEAST_EXPECT (to_string(f->condition()) == xc);

        auto subf = loadFulfillment (xsf);
        BEAST_EXPECT (subf);
        BEAST_EXPECT (to_string(*subf) == xsf);

        auto subc = loadCondition (xsc);
        BEAST_EXPECT (subc);
        BEAST_EXPECT (to_string(*subc) == xsc);

        // Now generate the binary versions and ensure
        // that they match what we expect. Then load them
        // and ensure they're identical:
        {
            auto const fblob1 = hexblob(
                "0001710d48656c6c6f20576f726c642120000460ec172b93ad5e563bf4"
                "932c70e1245034c35467ef2efd4d64ebf819683467e2bfb62291fad943"
                "2f8f298b9c4a4895dbe293f6ffda1a68dadf0ccdef5f47a0c7212a5fea"
                "3cda97a3f4c03ea9f2e8ac1cec86a51d452127abdba09d1b6f331c070a");

            auto const fblob2 = to_blob(*f);
            BEAST_EXPECT (fblob1 == fblob2);

            auto f2 = loadFulfillment(makeSlice(fblob2));
            BEAST_EXPECT (f2);
            BEAST_EXPECT (*f == *f2);
        }

        {
            auto const cblob1 = hexblob (
                "0001012520d4432da77614381660796de55f594e22801b523c7d99a52d"
                "cbd4cc2693205e8e016e");

            auto const cblob2 = to_blob(*c);
            BEAST_EXPECT (cblob1 == cblob2);

            auto c2 = loadCondition(makeSlice(cblob2));
            BEAST_EXPECT (c2);
            BEAST_EXPECT (*c == c2);
        }
    }

    void testNested()
    {
        testcase ("Nested");

        std::string const abc = "abc";
        std::string const def = "def";
        std::string const abcdef = abc + def;

        { // prefix ("abc", prefix ("def", ed25519 (..., "abcdef")))
            std::array<std::uint8_t, 32> sk =
            {{
                0x50, 0xd8, 0x58, 0xe0, 0x98, 0x5e, 0xcc, 0x7f,
                0x60, 0x41, 0x8a, 0xaf, 0x0c, 0xc5, 0xab, 0x58,
                0x7f, 0x42, 0xc2, 0x57, 0x0a, 0x88, 0x40, 0x95,
                0xa9, 0xe8, 0xcc, 0xac, 0xd0, 0xf6, 0x54, 0x5c
            }};

            auto edf = std::make_unique<Ed25519> (
                SecretKey{ sk }, makeSlice (abcdef));

            // Inner
            auto pif = std::make_unique<PrefixSha256>();
            pif->setPrefix(makeSlice (abc));
            pif->setSubfulfillment (std::move(edf));

            // Outer
            auto pof = std::make_unique<PrefixSha256>();
            pof->setPrefix(makeSlice (def));
            pof->setSubfulfillment (std::move (pif));

            auto const c = pof->condition();

            // The condition should validate with an empty
            // message, since the nested prefixes contain
            // the full message.
            check (*pof, c, {}, {});

            // It should fail with anything else.
            check (*pof, c, makeSlice (abc), {});
            check (*pof, c, makeSlice (def), {});
            check (*pof, c, makeSlice (abcdef), {});
        }

        { // prefix ("abc", prefix ("def", preimage (...)))
            auto const v = hexblob (
                "6B62BA0A77D5C7A423A5FC937EE5FF09");

            auto img = std::make_unique<PreimageSha256> (
                makeSlice (v));

            // Inner
            auto pif = std::make_unique<PrefixSha256>();
            pif->setPrefix(makeSlice (abc));
            pif->setSubfulfillment (std::move(img));

            // Outer
            auto pof = std::make_unique<PrefixSha256>();
            pof->setPrefix(makeSlice (def));
            pof->setSubfulfillment (std::move (pif));

            auto const c = pof->condition();

            // The condition should validate with any message
            // since it terminates at a preimage, which
            // validates for any message:
            check (*pof, c, {}, {});
            check (*pof, c, makeSlice (abc), makeSlice (abc));
            check (*pof, c, makeSlice (def), makeSlice (def));
            check (*pof, c, makeSlice (abcdef), makeSlice (abcdef));
        }
    }

    void run ()
    {
        testKnown ();
        testNested ();
        testPrefix ();
        testBinaryCodec ();
        testMalformedCondition ();
    }
};

BEAST_DEFINE_TESTSUITE (PrefixSha256, conditions, ripple);

}

}
