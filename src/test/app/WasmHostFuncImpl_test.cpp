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

#include <test/jtx.h>

#include <xrpld/app/misc/WasmHostFuncImpl.h>

namespace ripple {
namespace test {

std::array<std::uint8_t, 2>
toBytes(std::uint16_t value)
{
    std::array<std::uint8_t, 2> bytes = {
        static_cast<std::uint8_t>(value & 0xFF),
        static_cast<std::uint8_t>(value >> 8)};
    return bytes;
}

std::array<std::uint8_t, 4>
toBytes(std::uint32_t value)
{
    return {
        static_cast<std::uint8_t>(value & 0xFF),
        static_cast<std::uint8_t>((value >> 8) & 0xFF),
        static_cast<std::uint8_t>((value >> 16) & 0xFF),
        static_cast<std::uint8_t>((value >> 24) & 0xFF)};
}

struct WasmHostFuncImpl_test : public beast::unit_test::suite
{
    void
    testCacheLedgerObj()
    {
        testcase("cacheLedgerObj");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        STTx tx{ttESCROW_FINISH, [](STObject&) {}};
        test::StreamSink sink{beast::severities::kWarning};
        beast::Journal jlog{sink};
        ApplyContext ac{
            env.app(),
            ov,
            tx,
            tesSUCCESS,
            env.current()->fees().base,
            tapNONE,
            jlog};
        auto const dummyEscrow = keylet::escrow(env.master, 2);
        auto const accountKeylet = keylet::account(env.master);
        {
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            BEAST_EXPECT(
                hfs.cacheLedgerObj(accountKeylet.key, -1).error() ==
                HostFunctionError::SLOT_OUT_RANGE);
            BEAST_EXPECT(
                hfs.cacheLedgerObj(accountKeylet.key, 257).error() ==
                HostFunctionError::SLOT_OUT_RANGE);
            BEAST_EXPECT(
                hfs.cacheLedgerObj(dummyEscrow.key, 0).error() ==
                HostFunctionError::LEDGER_OBJ_NOT_FOUND);
            BEAST_EXPECT(hfs.cacheLedgerObj(accountKeylet.key, 0).value() == 1);

            for (int i = 1; i <= 256; ++i)
            {
                auto const result = hfs.cacheLedgerObj(accountKeylet.key, i);
                BEAST_EXPECT(result.has_value() && result.value() == i);
            }
            BEAST_EXPECT(
                hfs.cacheLedgerObj(accountKeylet.key, 0).error() ==
                HostFunctionError::SLOTS_FULL);
        }

        {
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            for (int i = 1; i <= 256; ++i)
            {
                auto const result = hfs.cacheLedgerObj(accountKeylet.key, 0);
                BEAST_EXPECT(result.has_value() && result.value() == i);
            }
            BEAST_EXPECT(
                hfs.cacheLedgerObj(accountKeylet.key, 0).error() ==
                HostFunctionError::SLOTS_FULL);
        }
    }

    void
    testGetTxField()
    {
        testcase("getTxField");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        STTx const stx = STTx(ttESCROW_FINISH, [&](auto& obj) {
            obj.setAccountID(sfAccount, env.master.id());
            obj.setAccountID(sfOwner, env.master.id());
            obj.setFieldU32(sfOfferSequence, env.seq(env.master));
            obj.setFieldU32(sfComputationAllowance, 1000);
        });
        test::StreamSink sink{beast::severities::kWarning};
        beast::Journal jlog{sink};
        ApplyContext ac{
            env.app(),
            ov,
            stx,
            tesSUCCESS,
            env.current()->fees().base,
            tapNONE,
            jlog};
        auto const dummyEscrow =
            keylet::escrow(env.master, env.seq(env.master));

        {
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);
            auto const account = hfs.getTxField(sfAccount);
            BEAST_EXPECT(
                account.has_value() &&
                std::equal(
                    account.value().begin(),
                    account.value().end(),
                    env.master.id().data()));
            auto const txType = hfs.getTxField(sfTransactionType);
            BEAST_EXPECT(
                txType.has_value() &&
                std::equal(
                    txType.value().begin(),
                    txType.value().end(),
                    toBytes(ttESCROW_FINISH).begin()));
            auto const offerSeq = hfs.getTxField(sfOfferSequence);
            BEAST_EXPECT(
                offerSeq.has_value() &&
                std::equal(
                    offerSeq.value().begin(),
                    offerSeq.value().end(),
                    toBytes(env.seq(env.master)).begin()));
            auto const compAllowance = hfs.getTxField(sfComputationAllowance);
            std::uint32_t const expectedAllowance = 1000;
            BEAST_EXPECT(
                compAllowance.has_value() &&
                std::equal(
                    compAllowance.value().begin(),
                    compAllowance.value().end(),
                    toBytes(expectedAllowance).begin()));
        }
    }

    void
    run() override
    {
        testCacheLedgerObj();
        testGetTxField();
    }
};

BEAST_DEFINE_TESTSUITE(WasmHostFuncImpl, app, ripple);

}  // namespace test
}  // namespace ripple
