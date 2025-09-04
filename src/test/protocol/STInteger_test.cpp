//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/TxFormats.h>

namespace ripple {

struct STInteger_test : public beast::unit_test::suite
{
    void
    testUInt8()
    {
        STUInt8 u8(42);
        BEAST_EXPECT(u8.value() == 42);
        BEAST_EXPECT(u8.getText() == "42");
        BEAST_EXPECT(u8.getSType() == STI_UINT8);
        BEAST_EXPECT(u8.getJson(JsonOptions::none) == 42);

        // there is some special handling for sfTransactionResult
        STUInt8 tr(sfTransactionResult, 0);
        BEAST_EXPECT(tr.value() == 0);
        BEAST_EXPECT(
            tr.getText() ==
            "The transaction was applied. Only final in a validated ledger.");
        BEAST_EXPECT(tr.getSType() == STI_UINT8);
        BEAST_EXPECT(tr.getJson(JsonOptions::none) == "tesSUCCESS");
    }

    void
    testUInt16()
    {
        STUInt16 u16(65535);
        BEAST_EXPECT(u16.value() == 65535);
        BEAST_EXPECT(u16.getText() == "65535");
        BEAST_EXPECT(u16.getSType() == STI_UINT16);
        BEAST_EXPECT(u16.getJson(JsonOptions::none) == 65535);

        // there is some special handling for sfLedgerEntryType
        STUInt16 let(sfLedgerEntryType, ltACCOUNT_ROOT);
        BEAST_EXPECT(let.value() == ltACCOUNT_ROOT);
        BEAST_EXPECT(let.getText() == "AccountRoot");
        BEAST_EXPECT(let.getSType() == STI_UINT16);
        BEAST_EXPECT(let.getJson(JsonOptions::none) == "AccountRoot");

        // there is some special handling for sfTransactionType
        STUInt16 tlt(sfTransactionType, ttPAYMENT);
        BEAST_EXPECT(tlt.value() == ttPAYMENT);
        BEAST_EXPECT(tlt.getText() == "Payment");
        BEAST_EXPECT(tlt.getSType() == STI_UINT16);
        BEAST_EXPECT(tlt.getJson(JsonOptions::none) == "Payment");
    }

    void
    testUInt32()
    {
        STUInt32 u32(1234567890);
        BEAST_EXPECT(u32.value() == 1234567890);
        BEAST_EXPECT(u32.getText() == "1234567890");
        BEAST_EXPECT(u32.getSType() == STI_UINT32);
        BEAST_EXPECT(u32.getJson(JsonOptions::none) == 1234567890);

        // there is some special handling for sfPermissionValue
        STUInt32 pv(sfPermissionValue, ttPAYMENT + 1);
        BEAST_EXPECT(pv.value() == ttPAYMENT + 1);
        BEAST_EXPECT(pv.getText() == "Payment");
        BEAST_EXPECT(pv.getSType() == STI_UINT32);
        BEAST_EXPECT(pv.getJson(JsonOptions::none) == "Payment");
        STUInt32 pv2(sfPermissionValue, PaymentMint);
        BEAST_EXPECT(pv2.value() == PaymentMint);
        BEAST_EXPECT(pv2.getText() == "PaymentMint");
        BEAST_EXPECT(pv2.getSType() == STI_UINT32);
        BEAST_EXPECT(pv2.getJson(JsonOptions::none) == "PaymentMint");
    }

    void
    testUInt64()
    {
        STUInt64 u64(0x123456789ABCDEF0ull);
        BEAST_EXPECT(u64.value() == 0x123456789ABCDEF0ull);
        BEAST_EXPECT(u64.getText() == "1311768467463790320");
        BEAST_EXPECT(u64.getSType() == STI_UINT64);

        // By default, getJson returns hex string
        auto jsonVal = u64.getJson(JsonOptions::none);
        BEAST_EXPECT(jsonVal.isString());
        BEAST_EXPECT(jsonVal.asString() == "123456789abcdef0");
    }

    void
    run() override
    {
        testUInt8();
        testUInt16();
        testUInt32();
        testUInt64();
    }
};

BEAST_DEFINE_TESTSUITE(STInteger, protocol, ripple);

}  // namespace ripple
