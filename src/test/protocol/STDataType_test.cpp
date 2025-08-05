//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STDataType.h>

#include <arpa/inet.h>  // For htonl/ntohl (needed for serialization simulation)

namespace ripple {
struct STDataType_test : public beast::unit_test::suite
{
    void
    testFields()
    {
        testcase("fields");

        auto const& sf = sfParameterType;

        {
            // STI_UINT8
            Serializer s;
            STDataType s1(sf);
            s1.setInnerSType(STI_UINT8);
            BEAST_EXPECT(s1.getInnerSType() == STI_UINT8);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "0010");
        }

        {
            // STI_UINT16
            Serializer s;
            STDataType s1(sf);
            s1.setInnerSType(STI_UINT16);
            BEAST_EXPECT(s1.getInnerSType() == STI_UINT16);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "0001");
        }

        {
            // STI_UINT32
            Serializer s;
            STDataType s1(sf);
            s1.setInnerSType(STI_UINT32);
            BEAST_EXPECT(s1.getInnerSType() == STI_UINT32);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "0002");
        }

        {
            // STI_UINT64
            Serializer s;
            STDataType s1(sf);
            s1.setInnerSType(STI_UINT64);
            BEAST_EXPECT(s1.getInnerSType() == STI_UINT64);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "0003");
        }

        {
            // STI_UINT128
            Serializer s;
            STDataType s1(sf);
            s1.setInnerSType(STI_UINT128);
            BEAST_EXPECT(s1.getInnerSType() == STI_UINT128);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "0004");
        }

        {
            // STI_UINT256
            Serializer s;
            STDataType s1(sf);
            s1.setInnerSType(STI_UINT256);
            BEAST_EXPECT(s1.getInnerSType() == STI_UINT256);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "0005");
        }

        {
            // STI_VL
            Serializer s;
            STDataType s1(sf);
            s1.setInnerSType(STI_VL);
            BEAST_EXPECT(s1.getInnerSType() == STI_VL);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "0007");
        }

        {
            // STI_ACCOUNT
            Serializer s;
            STDataType s1(sf);
            s1.setInnerSType(STI_ACCOUNT);
            BEAST_EXPECT(s1.getInnerSType() == STI_ACCOUNT);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "0008");
        }
        {
            // STI_AMOUNT (Native)
            Serializer s;
            STDataType s1(sf);
            s1.setInnerSType(STI_AMOUNT);
            BEAST_EXPECT(s1.getInnerSType() == STI_AMOUNT);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "0006");
        }
    }

    void
    run() override
    {
        testFields();
    }
};

BEAST_DEFINE_TESTSUITE(STDataType, protocol, ripple);

}  // namespace ripple
