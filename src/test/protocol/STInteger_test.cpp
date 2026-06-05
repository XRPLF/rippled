#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/TxFormats.h>

namespace xrpl {

struct STInteger_test : public beast::unit_test::Suite
{
    void
    testUInt8()
    {
        testcase("UInt8");
        STUInt8 const u8(255);
        BEAST_EXPECT(u8.value() == 255);
        BEAST_EXPECT(u8.getText() == "255");
        BEAST_EXPECT(u8.getSType() == STI_UINT8);
        BEAST_EXPECT(u8.getJson(JsonOptions::KNone) == 255);

        // there is some special handling for sfTransactionResult
        STUInt8 const tr(sfTransactionResult, 0);
        BEAST_EXPECT(tr.value() == 0);
        BEAST_EXPECT(
            tr.getText() == "The transaction was applied. Only final in a validated ledger.");
        BEAST_EXPECT(tr.getSType() == STI_UINT8);
        BEAST_EXPECT(tr.getJson(JsonOptions::KNone) == "tesSUCCESS");

        // invalid transaction result
        STUInt8 const tr2(sfTransactionResult, 255);
        BEAST_EXPECT(tr2.value() == 255);
        BEAST_EXPECT(tr2.getText() == "255");
        BEAST_EXPECT(tr2.getSType() == STI_UINT8);
        BEAST_EXPECT(tr2.getJson(JsonOptions::KNone) == 255);
    }

    void
    testUInt16()
    {
        testcase("UInt16");
        STUInt16 const u16(65535);
        BEAST_EXPECT(u16.value() == 65535);
        BEAST_EXPECT(u16.getText() == "65535");
        BEAST_EXPECT(u16.getSType() == STI_UINT16);
        BEAST_EXPECT(u16.getJson(JsonOptions::KNone) == 65535);

        // there is some special handling for sfLedgerEntryType
        STUInt16 const let(sfLedgerEntryType, ltACCOUNT_ROOT);
        BEAST_EXPECT(let.value() == ltACCOUNT_ROOT);
        BEAST_EXPECT(let.getText() == "AccountRoot");
        BEAST_EXPECT(let.getSType() == STI_UINT16);
        BEAST_EXPECT(let.getJson(JsonOptions::KNone) == "AccountRoot");

        // there is some special handling for sfTransactionType
        STUInt16 const tlt(sfTransactionType, ttPAYMENT);
        BEAST_EXPECT(tlt.value() == ttPAYMENT);
        BEAST_EXPECT(tlt.getText() == "Payment");
        BEAST_EXPECT(tlt.getSType() == STI_UINT16);
        BEAST_EXPECT(tlt.getJson(JsonOptions::KNone) == "Payment");
    }

    void
    testUInt32()
    {
        testcase("UInt32");
        STUInt32 const u32(4'294'967'295u);
        BEAST_EXPECT(u32.value() == 4'294'967'295u);
        BEAST_EXPECT(u32.getText() == "4294967295");
        BEAST_EXPECT(u32.getSType() == STI_UINT32);
        BEAST_EXPECT(u32.getJson(JsonOptions::KNone) == 4'294'967'295u);

        // there is some special handling for sfPermissionValue
        STUInt32 const pv(sfPermissionValue, ttPAYMENT + 1);
        BEAST_EXPECT(pv.value() == ttPAYMENT + 1);
        BEAST_EXPECT(pv.getText() == "Payment");
        BEAST_EXPECT(pv.getSType() == STI_UINT32);
        BEAST_EXPECT(pv.getJson(JsonOptions::KNone) == "Payment");
        STUInt32 const pv2(sfPermissionValue, PaymentMint);
        BEAST_EXPECT(pv2.value() == PaymentMint);
        BEAST_EXPECT(pv2.getText() == "PaymentMint");
        BEAST_EXPECT(pv2.getSType() == STI_UINT32);
        BEAST_EXPECT(pv2.getJson(JsonOptions::KNone) == "PaymentMint");
    }

    void
    testUInt64()
    {
        testcase("UInt64");
        STUInt64 const u64(0xFFFFFFFFFFFFFFFFull);
        BEAST_EXPECT(u64.value() == 0xFFFFFFFFFFFFFFFFull);
        BEAST_EXPECT(u64.getText() == "18446744073709551615");
        BEAST_EXPECT(u64.getSType() == STI_UINT64);

        // By default, getJson returns hex string
        auto jsonVal = u64.getJson(JsonOptions::KNone);
        BEAST_EXPECT(jsonVal.isString());
        BEAST_EXPECT(jsonVal.asString() == "ffffffffffffffff");

        STUInt64 const u642(sfMaximumAmount, 0xFFFFFFFFFFFFFFFFull);
        BEAST_EXPECT(u642.value() == 0xFFFFFFFFFFFFFFFFull);
        BEAST_EXPECT(u642.getText() == "18446744073709551615");
        BEAST_EXPECT(u642.getSType() == STI_UINT64);
        BEAST_EXPECT(u642.getJson(JsonOptions::KNone) == "18446744073709551615");
    }

    void
    testInt32()
    {
        testcase("Int32");
        {
            int const minInt32 = -2147483648;
            STInt32 const i32(minInt32);
            BEAST_EXPECT(i32.value() == minInt32);
            BEAST_EXPECT(i32.getText() == "-2147483648");
            BEAST_EXPECT(i32.getSType() == STI_INT32);
            BEAST_EXPECT(i32.getJson(JsonOptions::KNone) == minInt32);
        }

        {
            int const maxInt32 = 2147483647;
            STInt32 const i32(maxInt32);
            BEAST_EXPECT(i32.value() == maxInt32);
            BEAST_EXPECT(i32.getText() == "2147483647");
            BEAST_EXPECT(i32.getSType() == STI_INT32);
            BEAST_EXPECT(i32.getJson(JsonOptions::KNone) == maxInt32);
        }
    }

    void
    run() override
    {
        testUInt8();
        testUInt16();
        testUInt32();
        testUInt64();
        testInt32();
    }
};

BEAST_DEFINE_TESTSUITE(STInteger, protocol, xrpl);

}  // namespace xrpl
