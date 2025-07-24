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
#include <xrpl/protocol/STData.h>

#include <arpa/inet.h>  // For htonl/ntohl (needed for serialization simulation)

namespace ripple {
struct STData_test : public beast::unit_test::suite
{
    void
    testFields()
    {
        testcase("fields");

        auto const& sf = sfParameterValue;

        {
            // STI_UINT8
            Serializer s;
            unsigned char u8 = 1;

            STData s1(sf);
            s1.setFieldU8(u8);
            BEAST_EXPECT(s1.getFieldU8() == u8);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "001001");
            s.erase();

            STData s2(sf, u8);
            BEAST_EXPECT(s2.getFieldU8() == u8);
            s2.add(s);
            BEAST_EXPECT(strHex(s) == "001001");
        }

        {
            // STI_UINT16
            Serializer s;
            uint16_t u16 = 1U;

            STData s1(sf);
            s1.setFieldU16(u16);
            BEAST_EXPECT(s1.getFieldU16() == u16);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "00010001");
            s.erase();

            STData s2(sf, u16);
            BEAST_EXPECT(s2.getFieldU16() == u16);
            s2.add(s);
            BEAST_EXPECT(strHex(s) == "00010001");
        }

        {
            // STI_UINT32
            Serializer s;
            uint32_t u32 = 1U;

            STData s1(sf);
            s1.setFieldU32(u32);
            BEAST_EXPECT(s1.getFieldU32() == u32);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "000200000001");
            s.erase();

            STData s2(sf, u32);
            BEAST_EXPECT(s2.getFieldU32() == u32);
            s2.add(s);
            BEAST_EXPECT(strHex(s) == "000200000001");
        }

        {
            // STI_UINT64
            Serializer s;
            uint64_t u64 = 1U;

            STData s1(sf);
            s1.setFieldU64(u64);
            BEAST_EXPECT(s1.getFieldU64() == u64);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "00030000000000000001");
            s.erase();

            STData s2(sf, u64);
            BEAST_EXPECT(s2.getFieldU64() == u64);
            s2.add(s);
            BEAST_EXPECT(strHex(s) == "00030000000000000001");
        }

        {
            // STI_UINT128
            Serializer s;
            uint128 u128 = uint128(1);

            STData s1(sf);
            s1.setFieldH128(u128);
            BEAST_EXPECT(s1.getFieldH128() == u128);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "000400000000000000000000000000000001");
            s.erase();

            STData s2(sf, u128);
            BEAST_EXPECT(s2.getFieldH128() == u128);
            s2.add(s);
            BEAST_EXPECT(strHex(s) == "000400000000000000000000000000000001");
        }

        {
            // STI_UINT256
            Serializer s;
            uint256 u256 = uint256(1);

            STData s1(sf);
            s1.setFieldH256(u256);
            BEAST_EXPECT(s1.getFieldH256() == u256);
            s1.add(s);
            BEAST_EXPECT(
                strHex(s) ==
                "00050000000000000000000000000000000000000000000000000000000000"
                "000001");
            s.erase();

            STData s2(sf, u256);
            BEAST_EXPECT(s2.getFieldH256() == u256);
            s2.add(s);
            BEAST_EXPECT(
                strHex(s) ==
                "00050000000000000000000000000000000000000000000000000000000000"
                "000001");
        }
        {
            // STI_VL
            Serializer s;
            Blob blob = strUnHex("DEADBEEF").value();

            STData s1(sf);
            s1.setFieldVL(blob);
            BEAST_EXPECT(s1.getFieldVL() == blob);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "000704DEADBEEF");
            s.erase();

            STData s2(sf, blob);
            BEAST_EXPECT(s2.getFieldVL() == blob);
            s2.add(s);
            BEAST_EXPECT(strHex(s) == "000704DEADBEEF");
        }
        {
            // STI_ACCOUNT
            Serializer s;
            AccountID account = AccountID(1);

            STData s1(sf);
            s1.setAccountID(account);
            BEAST_EXPECT(s1.getAccountID() == account);
            s1.add(s);
            BEAST_EXPECT(
                strHex(s) == "0008140000000000000000000000000000000000000001");
            s.erase();

            AccountID account2 = AccountID(2);
            STData s2(sf, account2);
            BEAST_EXPECT(s2.getAccountID() == account2);
            s2.add(s);
            BEAST_EXPECT(
                strHex(s) == "0008140000000000000000000000000000000000000002");
        }
        {
            // STI_AMOUNT (Native)
            Serializer s;
            STAmount amount = STAmount(1);

            STData s1(sf);
            s1.setFieldAmount(amount);
            BEAST_EXPECT(s1.getFieldAmount() == amount);
            s1.add(s);
            BEAST_EXPECT(strHex(s) == "00064000000000000001");
            s.erase();

            STAmount amount2 = STAmount(2);
            STData s2(sf, amount2);
            BEAST_EXPECT(s2.getFieldAmount() == amount2);
            s2.add(s);
            BEAST_EXPECT(strHex(s) == "00064000000000000002");
        }
        {
            // STI_AMOUNT (IOU)
            Serializer s;
            IOUAmount iouamount1 = IOUAmount(1000);
            Issue const usd(
                Currency(0x5553440000000000),
                parseBase58<AccountID>("rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn")
                    .value());
            STAmount amount = STAmount(iouamount1, usd);

            STData s1(sf);
            s1.setFieldAmount(amount);
            BEAST_EXPECT(s1.getFieldAmount() == amount);
            s1.add(s);
            BEAST_EXPECT(
                strHex(s) ==
                "0006D5438D7EA4C680000000000000000000000000005553440000000000AE"
                "123A8556F3CF91154711376AFB0F894F832B3D");
            s.erase();

            IOUAmount iouamount2 = IOUAmount(2000);
            STAmount amount2 = STAmount(iouamount2, usd);
            STData s2(sf, amount2);
            BEAST_EXPECT(s2.getFieldAmount() == amount2);
            s2.add(s);
            BEAST_EXPECT(
                strHex(s) ==
                "0006D5471AFD498D00000000000000000000000000005553440000000000AE"
                "123A8556F3CF91154711376AFB0F894F832B3D");
        }
    }

    void
    run() override
    {
        testFields();
    }
};

BEAST_DEFINE_TESTSUITE(STData, protocol, ripple);

}  // namespace ripple
