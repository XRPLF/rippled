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
#include <ripple/conditions/PreimageSha256.h>
#include <algorithm>
#include <numeric>
#include <vector>

namespace ripple {
namespace cryptoconditions {

class PreimageSha256_test : public beast::unit_test::suite
{
    void
    check (
        std::vector<std::uint8_t> const& payload,
        std::string const& fulfillment,
        std::string const& condition)
    {
        auto f = loadFulfillment (fulfillment);
        auto c = loadCondition (condition);

        BEAST_EXPECT (f);
        BEAST_EXPECT (c);

        if (f && c)
        {
            // Ensure that loading works correctly
            BEAST_EXPECT (to_string (*f) == fulfillment);

            {
                auto f2 = loadFulfillment (makeSlice(to_blob (*f)));
                BEAST_EXPECT (f2);
                BEAST_EXPECT (*f == *f2);
            }

            BEAST_EXPECT (to_string (*c) == condition);

            {
                auto c1 = loadCondition (makeSlice(to_blob (*c)));
                BEAST_EXPECT (c1);
                BEAST_EXPECT (*c == *c1);

                auto c2 = loadCondition (to_string (*c));
                BEAST_EXPECT (c2);
                BEAST_EXPECT (*c == *c2);
            }

            // Ensures that the fulfillment generates
            // the condition correctly:
            BEAST_EXPECT (f->condition() == c);

            // Ensure that the fulfillment contains the
            // correct payload:
            BEAST_EXPECT (f->payload() == makeSlice(payload));

            // Check fulfillment
            BEAST_EXPECT (validate (*f, *c, {}));

            // Preimage ignores the message. Verify that it does:
            std::string test = "aaabbc";

            do
            {
                for (Slice t = makeSlice (test); !t.empty(); t += 1)
                    BEAST_EXPECT (validate (*f, *c, t));
            } while (std::next_permutation(test.begin(), test.end()));
        }
    }

    void testKnownVectors ()
    {
        testcase ("Known Vectors");

        check (hexblob (""),
            "cf:0:",
            "cc:0:3:47DEQpj8HBSa-_TImW-5JCeuQeRkm5NMpJWZG3hSuFU:0");
        check (hexblob ("00"),
            "cf:0:AA",
            "cc:0:3:bjQLnP-zepicpUTmu3gKLHiQHT-zNzh2hRGjBhevoB0:1");
        check (hexblob ("ff"),
            "cf:0:_w",
            "cc:0:3:qBAK5qoZQNC2Y7sxzUZhQuu9vVGHExuS2TgYmHgy64k:1");
        check (hexblob ("feff"),
            "cf:0:_v8",
            "cc:0:3:8ZdpKBDUV-KX_OnFZTsCWB_5mlCFI3DynX5f5H2dN-Y:2");
        check (hexblob ("fffe"),
            "cf:0:__4",
            "cc:0:3:s9UQ7wQnXKjmmOWzy7Ds45Se-SUvDNyDnp7jR0CaIgk:2");
        check (hexblob ("00ff"),
            "cf:0:AP8",
            "cc:0:3:But9amnuGeX733SQGNPSq_oEvL0TZdsxLrhtxxaTibg:2");
        check (hexblob ("0001"),
            "cf:0:AAE",
            "cc:0:3:tBP0fRPuL-bIRbLuFBr4HehY307FSaWLeXC7lmRbyNI:2");
        check (hexblob ("616263"),
            "cf:0:YWJj",
            "cc:0:3:ungWv48Bz-pBQUDeXa4iI7ADYaOWF3qctBD_YfIAFa0:3");
        check (hexblob ("f1f2f3f4f5f6f7f8f9fafbfcfdfeff"),
            "cf:0:8fLz9PX29_j5-vv8_f7_",
            "cc:0:3:ipyQ4jcC1AbAYiuzYDZ1YkAr4O1IxOe5XBKJdJ17nPA:15");

        std::vector<std::uint8_t> const v1 (256, 0x00);
        check (v1,
            "cf:0:"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAA",
            "cc:0:3:U0HmsmRpeacOV2UwB6HzEBaUIeyb3Z8aVkj3Wt4AWvE:256");

        std::vector<std::uint8_t> const v2 (256, 0xFF);
        check (v2,
            "cf:0:"
                "__________________________________________________________________________________"
                "__________________________________________________________________________________"
                "__________________________________________________________________________________"
                "__________________________________________________________________________________"
                "_____________w",
            "cc:0:3:PWh2oBRt6FdusjlahY3hIT0bksZbd53zozHP1aRYRUY:256");

        std::vector<std::uint8_t> v3 (256);
        std::iota (v3.begin(), v3.end(), std::uint8_t(0));

        check (v3,
            "cf:0:"
                "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD"
                "0-P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6"
                "e3x9fn-AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq-wsbKztLW2t7"
                "i5uru8vb6_wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t_g4eLj5OXm5-jp6uvs7e7v8PHy8_T1"
                "9vf4-fr7_P3-_w",
            "cc:0:3:QK_y6dLYki5Hr9RkjmlnSXFYeF-9Hahw5xECZr-USIA:256");

        std::vector<std::uint8_t> v4 (4096);
        std::iota (v4.begin(), v4.end(), std::uint8_t(0));

        check (v4,
            "cf:0:"
                "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD"
                "0-P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6"
                "e3x9fn-AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq-wsbKztLW2t7"
                "i5uru8vb6_wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t_g4eLj5OXm5-jp6uvs7e7v8PHy8_T1"
                "9vf4-fr7_P3-_wABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMj"
                "M0NTY3ODk6Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9w"
                "cXJzdHV2d3h5ent8fX5_gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp-goaKjpKWmp6ipqqusra"
                "6vsLGys7S1tre4ubq7vL2-v8DBwsPExcbHyMnKy8zNzs_Q0dLT1NXW19jZ2tvc3d7f4OHi4-Tl5ufo6err"
                "7O3u7_Dx8vP09fb3-Pn6-_z9_v8AAQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyAhIiMkJSYnKC"
                "kqKywtLi8wMTIzNDU2Nzg5Ojs8PT4_QEFCQ0RFRkdISUpLTE1OT1BRUlNUVVZXWFlaW1xdXl9gYWJjZGVm"
                "Z2hpamtsbW5vcHFyc3R1dnd4eXp7fH1-f4CBgoOEhYaHiImKi4yNjo-QkZKTlJWWl5iZmpucnZ6foKGio6"
                "SlpqeoqaqrrK2ur7CxsrO0tba3uLm6u7y9vr_AwcLDxMXGx8jJysvMzc7P0NHS09TV1tfY2drb3N3e3-Dh"
                "4uPk5ebn6Onq6-zt7u_w8fLz9PX29_j5-vv8_f7_AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh"
                "8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0-P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltc"
                "XV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn-AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZ"
                "qbnJ2en6ChoqOkpaanqKmqq6ytrq-wsbKztLW2t7i5uru8vb6_wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX"
                "2Nna29zd3t_g4eLj5OXm5-jp6uvs7e7v8PHy8_T19vf4-fr7_P3-_wABAgMEBQYHCAkKCwwNDg8QERITFB"
                "UWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFS"
                "U1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5_gIGCg4SFhoeIiYqLjI2Oj5"
                "CRkpOUlZaXmJmam5ydnp-goaKjpKWmp6ipqqusra6vsLGys7S1tre4ubq7vL2-v8DBwsPExcbHyMnKy8zN"
                "zs_Q0dLT1NXW19jZ2tvc3d7f4OHi4-Tl5ufo6err7O3u7_Dx8vP09fb3-Pn6-_z9_v8AAQIDBAUGBwgJCg"
                "sMDQ4PEBESExQVFhcYGRobHB0eHyAhIiMkJSYnKCkqKywtLi8wMTIzNDU2Nzg5Ojs8PT4_QEFCQ0RFRkdI"
                "SUpLTE1OT1BRUlNUVVZXWFlaW1xdXl9gYWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXp7fH1-f4CBgoOEhY"
                "aHiImKi4yNjo-QkZKTlJWWl5iZmpucnZ6foKGio6SlpqeoqaqrrK2ur7CxsrO0tba3uLm6u7y9vr_AwcLD"
                "xMXGx8jJysvMzc7P0NHS09TV1tfY2drb3N3e3-Dh4uPk5ebn6Onq6-zt7u_w8fLz9PX29_j5-vv8_f7_AA"
                "ECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0-"
                "P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3"
                "x9fn-AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq-wsbKztLW2t7i5"
                "uru8vb6_wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t_g4eLj5OXm5-jp6uvs7e7v8PHy8_T19v"
                "f4-fr7_P3-_wABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0"
                "NTY3ODk6Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcX"
                "JzdHV2d3h5ent8fX5_gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp-goaKjpKWmp6ipqqusra6v"
                "sLGys7S1tre4ubq7vL2-v8DBwsPExcbHyMnKy8zNzs_Q0dLT1NXW19jZ2tvc3d7f4OHi4-Tl5ufo6err7O"
                "3u7_Dx8vP09fb3-Pn6-_z9_v8AAQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyAhIiMkJSYnKCkq"
                "KywtLi8wMTIzNDU2Nzg5Ojs8PT4_QEFCQ0RFRkdISUpLTE1OT1BRUlNUVVZXWFlaW1xdXl9gYWJjZGVmZ2"
                "hpamtsbW5vcHFyc3R1dnd4eXp7fH1-f4CBgoOEhYaHiImKi4yNjo-QkZKTlJWWl5iZmpucnZ6foKGio6Sl"
                "pqeoqaqrrK2ur7CxsrO0tba3uLm6u7y9vr_AwcLDxMXGx8jJysvMzc7P0NHS09TV1tfY2drb3N3e3-Dh4u"
                "Pk5ebn6Onq6-zt7u_w8fLz9PX29_j5-vv8_f7_AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8g"
                "ISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0-P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV"
                "5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn-AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqb"
                "nJ2en6ChoqOkpaanqKmqq6ytrq-wsbKztLW2t7i5uru8vb6_wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2N"
                "na29zd3t_g4eLj5OXm5-jp6uvs7e7v8PHy8_T19vf4-fr7_P3-_wABAgMEBQYHCAkKCwwNDg8QERITFBUW"
                "FxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFSU1"
                "RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ent8fX5_gIGCg4SFhoeIiYqLjI2Oj5CR"
                "kpOUlZaXmJmam5ydnp-goaKjpKWmp6ipqqusra6vsLGys7S1tre4ubq7vL2-v8DBwsPExcbHyMnKy8zNzs"
                "_Q0dLT1NXW19jZ2tvc3d7f4OHi4-Tl5ufo6err7O3u7_Dx8vP09fb3-Pn6-_z9_v8AAQIDBAUGBwgJCgsM"
                "DQ4PEBESExQVFhcYGRobHB0eHyAhIiMkJSYnKCkqKywtLi8wMTIzNDU2Nzg5Ojs8PT4_QEFCQ0RFRkdISU"
                "pLTE1OT1BRUlNUVVZXWFlaW1xdXl9gYWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXp7fH1-f4CBgoOEhYaH"
                "iImKi4yNjo-QkZKTlJWWl5iZmpucnZ6foKGio6SlpqeoqaqrrK2ur7CxsrO0tba3uLm6u7y9vr_AwcLDxM"
                "XGx8jJysvMzc7P0NHS09TV1tfY2drb3N3e3-Dh4uPk5ebn6Onq6-zt7u_w8fLz9PX29_j5-vv8_f7_AAEC"
                "AwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0-P0"
                "BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9"
                "fn-AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq-wsbKztLW2t7i5ur"
                "u8vb6_wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t_g4eLj5OXm5-jp6uvs7e7v8PHy8_T19vf4"
                "-fr7_P3-_wABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NT"
                "Y3ODk6Ozw9Pj9AQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpbXF1eX2BhYmNkZWZnaGlqa2xtbm9wcXJz"
                "dHV2d3h5ent8fX5_gIGCg4SFhoeIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp-goaKjpKWmp6ipqqusra6vsL"
                "Gys7S1tre4ubq7vL2-v8DBwsPExcbHyMnKy8zNzs_Q0dLT1NXW19jZ2tvc3d7f4OHi4-Tl5ufo6err7O3u"
                "7_Dx8vP09fb3-Pn6-_z9_v8AAQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyAhIiMkJSYnKCkqKy"
                "wtLi8wMTIzNDU2Nzg5Ojs8PT4_QEFCQ0RFRkdISUpLTE1OT1BRUlNUVVZXWFlaW1xdXl9gYWJjZGVmZ2hp"
                "amtsbW5vcHFyc3R1dnd4eXp7fH1-f4CBgoOEhYaHiImKi4yNjo-QkZKTlJWWl5iZmpucnZ6foKGio6Slpq"
                "eoqaqrrK2ur7CxsrO0tba3uLm6u7y9vr_AwcLDxMXGx8jJysvMzc7P0NHS09TV1tfY2drb3N3e3-Dh4uPk"
                "5ebn6Onq6-zt7u_w8fLz9PX29_j5-vv8_f7_AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gIS"
                "IjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0-P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5f"
                "YGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn-AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ"
                "2en6ChoqOkpaanqKmqq6ytrq-wsbKztLW2t7i5uru8vb6_wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna"
                "29zd3t_g4eLj5OXm5-jp6uvs7e7v8PHy8_T19vf4-fr7_P3-_w",
            "cc:0:3:yPXQNB1U2VGnGxNubir8sU0R7YSJp64Sao_uDfbs8ZM:4096");
    }

    void testOverlong ()
    {
        testcase ("Fulfillment Maximum Payload Length");

        std::vector<std::uint8_t> v;

        // As long as we don't exceed the fulfillment length
        // we should succeed:
        unexcept ([this, &v]()
        {
            v.resize(maxSupportedFulfillmentLength - 1);
            PreimageSha256 h1 { makeSlice(v) };
            auto c1 = h1.condition();
            BEAST_EXPECT (c1.maxFulfillmentLength == h1.payload().size());

            v.resize(maxSupportedFulfillmentLength);
            PreimageSha256 h2 { makeSlice(v) };
            auto c2 = h2.condition();
            BEAST_EXPECT (c2.maxFulfillmentLength == h2.payload().size());
        });

        except ([this, &v] ()
        {
            v.resize(maxSupportedFulfillmentLength + 1);
            PreimageSha256 h3 { makeSlice(v) };
        });
    }

    void testFulfillment ()
    {
        testcase ("Fulfillment");

        std::vector<std::uint8_t> const v (256, 0x00);
        PreimageSha256 const f (makeSlice(v));

        // First check against incorrect conditions:
        char const* const ccs[] =
        {
            "cc:0:3:PWh2oBRt6FdusjlahY3hIT0bksZbd53zozHP1aRYRUY:256",
            "cc:1:25:XkflBmyISKuevH8-850LuMrzN-HT1Ds9zKUEzaZ2Wk0:103",
            "cc:2:2b:d3O4epRCo_3rj17Bf3v8hp5ig7vq84ivPok07T9Rdl0:146",
            "cc:3:11:uKkFs6dhGZCwD51c69vVvHYSp25cRi9IlvXfFaxhMjo:518",
            "cc:4:20:O2onvM62pC1io6jQKm8Nc2UyFXcd4kOmOsBIoYtZ2ik:96"
        };

        for (auto cc : ccs)
        {
            auto c = loadCondition (cc);

            if (BEAST_EXPECT (c))
            {
                BEAST_EXPECT (! validate (f, c.get(), {}));
                for (Slice m = makeSlice(v); !m.empty(); m += 1)
                    BEAST_EXPECT (! validate (f, c.get(), m));
            }
        }

        // Now, finally, check the correct condition:
        auto c = loadCondition (
            "cc:0:3:U0HmsmRpeacOV2UwB6HzEBaUIeyb3Z8aVkj3Wt4AWvE:256");

        if (BEAST_EXPECT (c))
        {
            // Note that the message may not, necessarily
            // have anything to do with the fulfillment, so
            // we expect the validation to succeed with any
            // message:
            BEAST_EXPECT (validate (f, c.get(), {}));
            for (Slice m = makeSlice(v); !m.empty(); m += 1)
                BEAST_EXPECT (validate (f, c.get(), m));
        }
    }

    void testMalformedCondition ()
    {
        testcase ("Malformed Condition");

        // This is malformed and will not load because a
        // feature suite of 0 is not supported.
        auto c1 = loadCondition (
            "cc:0:0:U0HmsmRpeacOV2UwB6HzEBaUIeyb3Z8aVkj3Wt4AWvE:256");
        BEAST_EXPECT (!c1);

        // The following will load but fail in different ways
        auto c2 = loadCondition ( // only sha256
            "cc:0:1:U0HmsmRpeacOV2UwB6HzEBaUIeyb3Z8aVkj3Wt4AWvE:256");
        BEAST_EXPECT (c2 && !validate(*c2));

        auto c3 = loadCondition ( // only preimage
            "cc:1:2:Yja3qFj7NS_VwwE7aJjPJos-uFCzStJlJLD4VsNy2XM:1");
        BEAST_EXPECT (c3 && !validate(*c3));

        auto c4 = loadCondition ( // only prefix+sha256
            "cc:1:20:Yja3qFj7NS_VwwE7aJjPJos-uFCzStJlJLD4VsNy2XM:1");
        BEAST_EXPECT (c4 && !validate(*c4));
    }

    void run ()
    {
        testKnownVectors ();
        testOverlong ();
        testFulfillment ();
        testMalformedCondition ();
    }
};

BEAST_DEFINE_TESTSUITE (PreimageSha256, conditions, ripple);

}

}
