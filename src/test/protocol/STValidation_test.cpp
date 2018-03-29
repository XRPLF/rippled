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

#include <BeastConfig.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/st.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/beast/unit_test.h>
#include <test/jtx.h>

#include <memory>
#include <type_traits>

namespace ripple {

class STValidation_test : public beast::unit_test::suite
{
public:
    void testDeserialization ()
    {
        testcase ("Deserialization");

        constexpr unsigned char payload1[] =
        { // specifies an Ed25519 public key
            0x72, 0x00, 0x73, 0x21, 0xed, 0x78, 0x00, 0xe6, 0x73, 0x00, 0x72, 0x00, 0x3c, 0x00, 0x00, 0x00,
            0x88, 0x00, 0xe6, 0x73, 0x38, 0x00, 0x00, 0x8a, 0x00, 0x88, 0x4e, 0x31, 0x30, 0x5f, 0x5f, 0x63,
            0x78, 0x78, 0x61, 0x62, 0x69, 0x76, 0x31, 0x30, 0x37, 0x5f, 0x5f, 0x63, 0x6c, 0x61, 0x73, 0x73,
            0x5f, 0x74, 0x79, 0x70, 0x65, 0x5f, 0x69, 0x6e, 0x66, 0x6f, 0x45, 0x00, 0xe6, 0x88, 0x54, 0x72,
            0x75, 0x73, 0x74, 0x53, 0x65, 0x74, 0x65, 0x61, 0x74, 0x65, 0x88, 0x00, 0xe6, 0x88, 0x00, 0xe6,
            0x73, 0x00, 0x72, 0x00, 0x8a, 0x00, 0x88, 0x00, 0xe6
        };

        constexpr unsigned char payload2[] =
        {
            0x73, 0x21, 0xed, 0xff, 0x03, 0x1c, 0xbe, 0x65, 0x22, 0x61, 0x9c, 0x5e, 0x13, 0x12, 0x00, 0x3b,
            0x43, 0x00, 0x00, 0x00, 0xf7, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x13, 0x13, 0x13,
            0x3a, 0x27, 0xff
        };

        constexpr unsigned char payload3[] =
        { // Has no public key at all
            0x72, 0x00, 0x76, 0x31, 0x30, 0x37, 0x5f, 0x5f, 0x63, 0x6c, 0x61, 0x73, 0x73, 0x5f, 0x74, 0x79,
            0x70, 0x65, 0x5f, 0x69, 0x6e, 0x66, 0x6f, 0x45, 0x00, 0xe6, 0x88, 0x54, 0x72, 0x75, 0x73, 0x74,
            0x53, 0x65, 0x74, 0x65, 0x61, 0x74, 0x65, 0x88, 0x00, 0xe6, 0x88, 0x00, 0xe6, 0x73, 0x00, 0x72,
            0x00, 0x8a, 0x00, 0x88, 0x00, 0xe6
        };

        try
        {
            SerialIter sit{payload1, sizeof(payload1)};
            auto stx = std::make_shared<ripple::STValidation>(sit,
                [](PublicKey const& pk) {
                    return calcNodeID(pk);
                }, false);
            fail("An exception should have been thrown");
        }
        catch (std::exception const& e)
        {
            BEAST_EXPECT(strcmp(e.what(), "Invalid public key in validation") == 0);
        }

        try
        {
            SerialIter sit{payload2, sizeof(payload2)};
            auto stx = std::make_shared<ripple::STValidation>(sit,
                [](PublicKey const& pk) {
                    return calcNodeID(pk);
                }, false);
            fail("An exception should have been thrown");
        }
        catch (std::exception const& e)
        {
            BEAST_EXPECT(strcmp(e.what(), "Invalid public key in validation") == 0);
        }

        try
        {
            SerialIter sit{payload3, sizeof(payload3)};
            auto stx = std::make_shared<ripple::STValidation>(sit,
                [](PublicKey const& pk) {
                    return calcNodeID(pk);
                }, false);
            fail("An exception should have been thrown");
        }
        catch (std::exception const& e)
        {
            log << e.what() << "\n";
            BEAST_EXPECT(strcmp(e.what(), "Invalid public key in validation") == 0);
        }
    }

    void
    run()
    {
        testDeserialization();
    }
};

BEAST_DEFINE_TESTSUITE(STValidation,protocol,ripple);

} // ripple
