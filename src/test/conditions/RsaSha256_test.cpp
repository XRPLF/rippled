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
#include <ripple/conditions/RsaSha256.h>
#include <algorithm>

namespace ripple {
namespace cryptoconditions {

class RsaSha256_test : public beast::unit_test::suite
{
    // A well-known message, its fulfillment and its condition
    std::string const knownMessage = "aaa";

    std::string const knownFulfillment =
        "cf:3:ggEA4e-LJNb3awnIHtd1KqJi8ETwSodNQ4CdMc6mEvmbDJeotDdBU-Pu89ZmF"
        "oQ-DkHCkyZLcbYXPbHPDWzVWMWGV3Bvzwl_cExIPlnL_f1bPue8gNdAxeDwR_PoX8D"
        "XWBV3am8_I8XcXnlxOaaILjgzakpfs2E3Yg_zZj264yhHKAGGL3Ly-HsgK5yJrdfNW"
        "woHb3xT41A59n7RfsgV5bQwXMYxlwaNXm5Xm6beX04-V99eTgcv8s5MZutFIzlzh1J"
        "1ljnwJXv1fb1cRD-1FYzOCj02rce6AfM6C7bbsr-YnWBxEvI0TZk-d-VjwdNh3t9X2"
        "pbvLPxoXwArY4JGpbMJuYIBAEjolF7-AHVW1b9NXySeSAj3MH4pUR0yYtrvYdiAmPm"
        "qSovAYjqMl1c49l1r9FnVQ_KJ1zy8evTqOjP78-xEQER5EdcilAkeVhgzYo5Jp3LtY"
        "I3mxEWVqR4-F9bPXsOyUo1j0q3WRjmJsS7sV332Rwlg32gyqdhMNg0cIXrWTIYlvbW"
        "U-wraCGzey73lgNQkv5dG0vDDEoJtu7AK1otSxMt9RxVro146mByXOGN5LMgNBKGAI"
        "QpSQVhltks6YXdLHTl114qYsIIe5Vyg-GMF1CUp4Q6wFc79QC-1myq7je7lKm8kR9I"
        "oRgPSGc1OjPnP_dVJiInDeAtZ3WpX731zJiA";

    std::string const knownCondition =
        "cc:3:11:uKkFs6dhGZCwD51c69vVvHYSp25cRi9IlvXfFaxhMjo:518";

    // Some RSA keys we use to check
    std::string const goodKey =
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "MIIEpAIBAAKCAQEAq9QZZzSmdXaAFeSkUgK8/xuyKPQEFNkiEzatMSmmGN+DpCR7\n"
        "HAK4W3wHfW6jegQFPlsvWLWbtnwgwCHhv1oW4jiL7BDD3prJIuJmhCE/w6WPKTFb\n"
        "WhvxQY5sbqCDnjcd0x/adKjNLaTpSRANscR+hahbQA1vqPperHz/Z20reRPQ6aDn\n"
        "w+qBL7dnVFgCPu8QrueyaZ0I5xQMIJiF0CnLXbPbU0ybxDVNgBDXsYeHE4VEM7ek\n"
        "NQAMrsr6wodD5y94jynXHharEv5dzKsQFPRAGKzTvwQqWSiZr+Fgq4q1GqQW+oUa\n"
        "xssbXWkGHEOxPBz6RFqLOFd8JnM9yVlWs2nWDwIDAQABAoIBAQCW9WNQCbCImBBV\n"
        "q6c1qdQzaDiwxBjl3BGUwb+M5qNXTN9RkP9bj4Q6U5AdAdu7sdaNfvzsubjQrOL1\n"
        "CY9UVqiuHLHJNr1uT5yP+knIoZFsqIJK1WMFmnDtgFwBISIhGRkpx91cCoUgKbcO\n"
        "in0Nha0Gbe+lKWjFExmj/rlAO2grGO4yYd+P27BZ99mHBXPMQIIwQbSeRUTBLiRy\n"
        "VhN7Mb60wag2m4F9zriEzhcj7pePNKHvpqNiuT5FCVoUNZW2CqFoXgEghF1VdWj5\n"
        "UovZITUCN9zrGdFHWQj3Hx1LZo3UQz3auUp4XQ89dIm1GefqcZYpnzth+D43UXtC\n"
        "f33nK9shAoGBANOm6aAhAh8Ahtc+52u3ykTnRGwI+H3QvgvzE7keZGIUb0yKtd/+\n"
        "yuxYI3DgN/Mn69p3pLrYh/CJ9VonhELYv4oekGZfCmqEtUxmkTa9EgTKAIxIDw9W\n"
        "t/jNBzzBccF22kl9w+nYHNOqo8M9yUlx0xwLnfioVX45G4jouucfPUDxAoGBAM/V\n"
        "CmykuH8vIYluyobldKFglXKeFMQKlKG+Dv9wG71RSYPyamd7lu3fPgZyGcbAGd49\n"
        "/Wewpq8ieagjrTuCEGlI0lhrL35axvBDKQcS+LTp+uD6vpxnFm986cxLgYRTNq0/\n"
        "eMUvJy72Ms5zajZUMdjM/nqTA9zpVDofL+xb6Ib/AoGAVheU/H+wvy+VqcR6mgRe\n"
        "kHyKBm/3tCXOyEmOAkTsjEDHrRjXNlAL9us7L1TlLVFVzL3SEfa2BQ/47z0XvaEw\n"
        "+FvKXPnX4NAudu9ZrixmQfBxHJ7LEXAy0U+E3B/Lx+gyjqZLpLk1sJu+lVJyqB9W\n"
        "whevoE/Ixtkv7BbOv+ijH+ECgYAb0WQnzpRzUZenkZDCJYxK3WajhM06wD/MtmfD\n"
        "gPn1iR/R7WyYlU5KYIsoybTxiVztBlcYvehRoMev3baeNHaF4R1mgFJHE1d1aUfg\n"
        "joWDkZ3m5ykEPjgejBWvJpwbXhf/cHN10S3pd0Ktp30b8IELh8S4G111ADYp4WrE\n"
        "tDiXeQKBgQCROYcVuUEfkOiKeGHkBGbbsgj77KzZ1x7wUBzueKAND9+e2kX8kcc2\n"
        "lX9DDkShUvZau0EJtQsFTehsZZeeBwtSvWu1A+2Wn9D19Hxe0qJCNZ6bqYQM9i2S\n"
        "JjI6wG7YYl/foT3B3Zf3A3G9gUOc+P5/dnUi+6r+l7GUvQ5WnVQjDw==\n"
        "-----END RSA PRIVATE KEY-----\n";

    std::string const shortKey =
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "MGMCAQACEQCtbMmYUOdPy+XwgP+xXzfrAgMBAAECEQCFPVJ5GpdMnxfbcKFUUb2\n"
        "JAgkA2e+AQuY6Ns8CCQDLtxhLU8j0JQIJAKkphE9pUUp1AghYypxPMNy09QIISr\n"
        "srXHy9nPk=\n"
        "-----END RSA PRIVATE KEY-----\n";

    std::string const longKey =
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "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\n"
        "QIpo2Qox/w==\n"
        "-----END RSA PRIVATE KEY-----\n";

    void check (
        Fulfillment const& f,
        Condition const& c,
        Slice test,
        Slice good)
    {
        BEAST_EXPECT (validate (f, c, test) ==
            ((test == good) && (f.condition() == c)));
    }

    void testKnown ()
    {
        testcase ("Known");

        Slice m = makeSlice (knownMessage);
        std::string test = "aaabc";

        // Load and test string and binary and text
        // serialization & deserialization
        auto const f = loadFulfillment(knownFulfillment);
        BEAST_EXPECT (f);
        BEAST_EXPECT (to_string (*f) == knownFulfillment);

        {
            auto const f2 = loadFulfillment(makeSlice(to_blob(*f)));
            BEAST_EXPECT (f2);
            BEAST_EXPECT (*f == *f2);
        }

        // Verify the condition for this fulfillment and test
        // binary and text serialization & deserialization
        auto const c = f->condition();
        BEAST_EXPECT (to_string(c) == knownCondition);

        {
            auto c1 = loadCondition (knownCondition);
            BEAST_EXPECT (c1);
            BEAST_EXPECT (c == *c1);

            auto c2 = loadCondition (makeSlice(to_blob(c)));
            BEAST_EXPECT (c2);
            BEAST_EXPECT (c == *c2);
        }

        // First check against incorrect conditions, using both
        // correct, incorrect and empty buffers.
        char const* const ccs[] =
        {
            "cc:0:3:PWh2oBRt6FdusjlahY3hIT0bksZbd53zozHP1aRYRUY:256",
            "cc:1:25:XkflBmyISKuevH8-850LuMrzN-HT1Ds9zKUEzaZ2Wk0:103",
            "cc:2:2b:d3O4epRCo_3rj17Bf3v8hp5ig7vq84ivPok07T9Rdl0:146",
            "cc:3:11:Mjmrcm06fOo-3WOEZu9YDSNfqmn0lj4iOsTVEurtCdI:518",
            "cc:4:20:O2onvM62pC1io6jQKm8Nc2UyFXcd4kOmOsBIoYtZ2ik:96"
        };

        for (auto cc : ccs)
        {
            auto nc = loadCondition (cc);

            if (BEAST_EXPECT (nc && nc != c))
            {
                check (*f, nc.get(), makeSlice(test), m);
                check (*f, nc.get(), m, m);
                check (*f, nc.get(), { }, m);
            }
        }

        // Now try against the correct condition with various
        // buffers - most are incorrect, some are correct.
        do
        {
            for (Slice t = makeSlice (test); !t.empty(); t += 1)
                check (*f, c, t, m);
        } while (std::next_permutation(test.begin(), test.end()));

        // And with an empty buffer:
        check (*f, c, Slice { }, m);

        // Under the existing spec, multiple messages sharing
        // the same key should generate the same fulfillment:
        {
            std::array<std::uint8_t, 3> aaa {{ 'a', 'a', 'a' }};
            std::array<std::uint8_t, 3> bbb {{ 'b', 'b', 'b' }};

            RsaSha256 f1;
            BEAST_EXPECT (f1.sign (goodKey, makeSlice (aaa)));

            RsaSha256 f2;
            BEAST_EXPECT (f2.sign (goodKey, makeSlice (bbb)));

            BEAST_EXPECT (f1.condition () == f2.condition ());
        }
    }

    void testDynamic ()
    {
        testcase ("Dynamic");

        Slice m = makeSlice (knownMessage);

        std::string test = "aaabc";

        RsaSha256 f;
        BEAST_EXPECT (f.sign (goodKey, m));

        {
            auto const f2 = loadFulfillment(makeSlice(to_blob(f)));
            BEAST_EXPECT (f2);
            BEAST_EXPECT (f == *f2);
        }

        // Generate and verify the condition for this fulfillment:
        auto const c = f.condition();

        {
            auto c1 = loadCondition (to_string(c));
            BEAST_EXPECT (c1);
            BEAST_EXPECT (c == *c1);

            auto c2 = loadCondition (makeSlice(to_blob(c)));
            BEAST_EXPECT (c2);
            BEAST_EXPECT (c == *c2);
        }

        // First check against incorrect conditions, using both
        // correct, incorrect and empty buffers.
        char const* const ccs[] =
        {
            "cc:0:3:PWh2oBRt6FdusjlahY3hIT0bksZbd53zozHP1aRYRUY:256",
            "cc:1:25:XkflBmyISKuevH8-850LuMrzN-HT1Ds9zKUEzaZ2Wk0:103",
            "cc:2:2b:d3O4epRCo_3rj17Bf3v8hp5ig7vq84ivPok07T9Rdl0:146",
            "cc:3:11:Mjmrcm06fOo-3WOEZu9YDSNfqmn0lj4iOsTVEurtCdI:518",
            "cc:4:20:O2onvM62pC1io6jQKm8Nc2UyFXcd4kOmOsBIoYtZ2ik:96"
        };

        for (auto cc : ccs)
        {
            auto nc = loadCondition (cc);

            if (BEAST_EXPECT (nc && nc != c))
            {
                check (f, nc.get(), makeSlice(test), m);
                check (f, nc.get(), m, m);
                check (f, nc.get(), { }, m);
            }
        }

        // Now try against the correct condition with various
        // buffers - most are incorrect, some are correct.
        do
        {
            for (Slice t = makeSlice (test); !t.empty(); t += 1)
                check (f, c, t, m);
        } while (std::next_permutation(test.begin(), test.end()));

        // And with an empty buffer:
        check (f, c, Slice { }, m);
    }

    void testKeySize ()
    {
        testcase ("Key Sizes");

        RsaSha256 f1;
        BEAST_EXPECT (!f1.sign (longKey, makeSlice (knownMessage)));

        RsaSha256 f2;
        BEAST_EXPECT (!f2.sign (shortKey, makeSlice (knownMessage)));
    }

    void testMalformedCondition ()
    {
        testcase ("Malformed Condition");

        // This is malformed and will not load because a
        // feature suite of 0 is not supported.
        auto c1 = loadCondition (
            "cc:3:0:Mjmrcm06fOo-3WOEZu9YDSNfqmn0lj4iOsTVEurtCdI:518");
        BEAST_EXPECT (!c1);

        // The following will load but fail in different ways
        auto c2 = loadCondition ( // only sha256
            "cc:3:1:Mjmrcm06fOo-3WOEZu9YDSNfqmn0lj4iOsTVEurtCdI:518");
        BEAST_EXPECT (c2 && !validate(*c2));

        auto c3 = loadCondition ( // only preimage
            "cc:3:2:Mjmrcm06fOo-3WOEZu9YDSNfqmn0lj4iOsTVEurtCdI:518");
        BEAST_EXPECT (c3 && !validate(*c3));

        auto c4 = loadCondition ( // sha256+preimage
            "cc:4:3:RCmTBlAEqh5MSPTdAVgZTAI0m8xmTNluQA6iaZGKjVE:96");
        BEAST_EXPECT (c4 && !validate(*c4));

        auto c5 = loadCondition ( // Ed25519+sha256+preimage
            "cc:1:23:Yja3qFj7NS_VwwE7aJjPJos-uFCzStJlJLD4VsNy2XM:1");
        BEAST_EXPECT (c5 && !validate(*c5));

        auto c6 = loadCondition ( // rsa+sha256+threshold
            "cc:1:19:Yja3qFj7NS_VwwE7aJjPJos-uFCzStJlJLD4VsNy2XM:1");
        BEAST_EXPECT (c6 && !validate(*c6));

        auto c7 = loadCondition ( // rsa
            "cc:1:10:Yja3qFj7NS_VwwE7aJjPJos-uFCzStJlJLD4VsNy2XM:1");
        BEAST_EXPECT (c7 && !validate(*c7));
    }

    void run ()
    {
        testKnown();
        testDynamic();
        testKeySize();
        testMalformedCondition();
    }
};

BEAST_DEFINE_TESTSUITE (RsaSha256, conditions, ripple);

}

}
